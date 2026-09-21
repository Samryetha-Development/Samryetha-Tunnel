use axum::body::{Body, Bytes};
use axum::extract::{ConnectInfo, Request, State};
use axum::http::{HeaderMap, StatusCode};
use axum::response::{IntoResponse, Response};
use std::collections::HashMap;
use std::net::{IpAddr, SocketAddr};
use std::time::Duration;
use tokio::sync::mpsc;
use tracing::warn;

use crate::auth::AppState;
use crate::models::VisitorAuth;
use crate::protocol::StreamEvent;
use crate::routes_ws::filter_headers;

fn client_ip(headers: &HeaderMap, fallback: SocketAddr) -> IpAddr {
    if let Some(xff) = headers.get("x-forwarded-for").and_then(|v| v.to_str().ok()) {
        if let Some(first) = xff.split(',').next() {
            if let Ok(ip) = first.trim().parse::<IpAddr>() {
                return ip;
            }
        }
    }
    fallback.ip()
}

fn ip_in_list(ip: IpAddr, list: &[String]) -> bool {
    list.iter().any(|entry| {
        let entry = entry.trim();
        if entry.is_empty() {
            return false;
        }
        if let Some((net, len)) = entry.split_once('/') {
            if let (Ok(net_ip), Ok(bits)) = (net.parse::<IpAddr>(), len.parse::<u8>()) {
                return cidr_contains(net_ip, bits, ip);
            }
            false
        } else {
            entry.parse::<IpAddr>().map(|e| e == ip).unwrap_or(false)
        }
    })
}

fn cidr_contains(net: IpAddr, bits: u8, ip: IpAddr) -> bool {
    match (net, ip) {
        (IpAddr::V4(n), IpAddr::V4(i)) => {
            if bits > 32 {
                return false;
            }
            let mask = if bits == 0 { 0 } else { u32::MAX << (32 - bits) };
            (u32::from(n) & mask) == (u32::from(i) & mask)
        }
        (IpAddr::V6(n), IpAddr::V6(i)) => {
            if bits > 128 {
                return false;
            }
            let mask = if bits == 0 { 0 } else { u128::MAX << (128 - bits) };
            (u128::from(n) & mask) == (u128::from(i) & mask)
        }
        _ => false,
    }
}

pub async fn visitor_handler(
    State(state): State<AppState>,
    ConnectInfo(addr): ConnectInfo<SocketAddr>,
    req: Request,
) -> Response {
    let host = req
        .headers()
        .get("host")
        .and_then(|v| v.to_str().ok())
        .unwrap_or("")
        .to_string();
    let method = req.method().to_string();
    let path = req.uri().path().to_string();
    let query = req.uri().query().map(|q| q.to_string());
    let req_headers = req.headers().clone();

    let Some((target, forward_path)) = state.registry.lookup(&host, &path, &state.cfg.base_domain).await else {
        // 不提供网页控制台：所有管理操作请用 GUI / CLI
        let hostname = host.split(':').next().unwrap_or(&host).to_lowercase();
        if hostname == state.cfg.base_domain && path == "/" {
            return (
                StatusCode::OK,
                "samryetha tunnel server\n\n本服务不提供网页控制台。\n请使用 GUI 或 CLI 管理（/auth/*、/api/*）。\n",
            )
                .into_response();
        }
        return (StatusCode::NOT_FOUND, "no such tunnel (check subdomain/path)").into_response();
    };

    // ---- 访客侧鉴权 ----
    if let Some(raw) = &target.visitor_auth {
        if let Ok(va) = serde_json::from_str::<VisitorAuth>(raw) {
            if let Some(basic) = &va.basic {
                let ok = req_headers
                    .get(axum::http::header::AUTHORIZATION)
                    .and_then(|v| v.to_str().ok())
                    .and_then(crate::util::parse_basic_auth)
                    .map(|(u, p)| u == basic.user && p == basic.pass)
                    .unwrap_or(false);
                if !ok {
                    return (
                        StatusCode::UNAUTHORIZED,
                        [("WWW-Authenticate", "Basic realm=\"tunnel\"")],
                        "需要访客认证",
                    )
                        .into_response();
                }
            }
            if !va.ips.is_empty() {
                let ip = client_ip(&req_headers, addr);
                if !ip_in_list(ip, &va.ips) {
                    return (StatusCode::FORBIDDEN, "来源 IP 不在白名单").into_response();
                }
            }
        }
    }

    // ---- 限流 ----
    if !state.limiter.allow(target.user_id).await {
        return (StatusCode::TOO_MANY_REQUESTS, "请求过快，稍后再试").into_response();
    }

    // ---- 配额 ----
    let limit: Option<(i64,)> = sqlx::query_as("SELECT daily_bytes FROM users WHERE id = ?")
        .bind(target.user_id)
        .fetch_optional(&state.db)
        .await
        .unwrap_or(None);
    let daily_limit = limit.map(|r| r.0).unwrap_or(i64::MAX);
    let (used, _) = state.usage.today(target.user_id).await;
    if used >= daily_limit {
        return (StatusCode::TOO_MANY_REQUESTS, "该用户今日流量配额已用尽").into_response();
    }

    // ---- 并发流上限 ----
    if !state.stream_acquire(target.user_id).await {
        return (StatusCode::TOO_MANY_REQUESTS, "该用户并发连接数已达上限").into_response();
    }

    // 请求体（v1 仍限 5MB；流式上传后续版本）
    let body_bytes: Bytes = match axum::body::to_bytes(req.into_body(), 5 * 1024 * 1024).await {
        Ok(b) => b,
        Err(_) => {
            state.stream_release(target.user_id).await;
            return (StatusCode::PAYLOAD_TOO_LARGE, "body > 5MB").into_response();
        }
    };

    let forward_full = match &query {
        Some(q) => format!("{forward_path}?{q}"),
        None => forward_path.clone(),
    };

    let mut headers = HashMap::new();
    for (k, v) in req_headers.iter() {
        if let Ok(vs) = v.to_str() {
            headers.insert(k.to_string(), vs.to_string());
        }
    }
    if let Some(strip) = &target.strip_prefix {
        headers.insert("x-forwarded-prefix".to_string(), strip.clone());
        headers.insert("x-forwarded-host".to_string(), host.clone());
    }

    let stream_id = state.registry.next_stream_id().await;
    let (tx, mut rx) = mpsc::unbounded_channel::<StreamEvent>();
    state.registry.insert_pending(stream_id, tx).await;

    let open = crate::protocol::ServerMsg::OpenStream {
        stream_id,
        tunnel_id: target.tunnel_id.clone(),
        proto: target.proto.clone(),
        method: method.clone(),
        path: forward_full,
        headers: filter_headers(&headers),
        body: body_bytes.to_vec(),
    };
    if !state.registry.send_to_conn(target.conn_id, open).await {
        state.registry.take_pending(stream_id).await;
        state.stream_release(target.user_id).await;
        return (StatusCode::BAD_GATEWAY, "client offline").into_response();
    }
    state.usage.add(target.user_id, 0, 1).await;

    // 等响应头
    let first = match tokio::time::timeout(Duration::from_secs(30), rx.recv()).await {
        Ok(Some(ev)) => ev,
        Ok(None) => {
            state.registry.take_pending(stream_id).await;
            state.stream_release(target.user_id).await;
            return (StatusCode::BAD_GATEWAY, "client closed stream").into_response();
        }
        Err(_) => {
            state.registry.take_pending(stream_id).await;
            state.stream_release(target.user_id).await;
            warn!("tunnel timeout stream={stream_id}");
            return (StatusCode::GATEWAY_TIMEOUT, "本地服务响应超时").into_response();
        }
    };

    let (status, out_headers) = match first {
        StreamEvent::Head { status, headers } => (status, headers),
        StreamEvent::Abort(reason) => {
            state.registry.take_pending(stream_id).await;
            state.stream_release(target.user_id).await;
            return (StatusCode::BAD_GATEWAY, format!("本地服务错误: {reason}")).into_response();
        }
        StreamEvent::End => {
            state.registry.take_pending(stream_id).await;
            state.stream_release(target.user_id).await;
            return (StatusCode::BAD_GATEWAY, "本地服务未返回响应头").into_response();
        }
        StreamEvent::Chunk(_) => {
            // 不该发生（客户端必须先发 head），忽略并按 502 处理
            state.registry.take_pending(stream_id).await;
            state.stream_release(target.user_id).await;
            return (StatusCode::BAD_GATEWAY, "协议错误：缺少响应头").into_response();
        }
    };

    let mut hm = HeaderMap::new();
    for (k, v) in out_headers {
        if let (Ok(hn), Ok(hv)) = (
            axum::http::HeaderName::try_from(k.to_lowercase()),
            axum::http::HeaderValue::from_str(&v),
        ) {
            if hn == "content-length" || hn == "connection" || hn == "transfer-encoding" {
                continue;
            }
            hm.insert(hn, hv);
        }
    }

    // 流式 body：SSE / chunked 友好，逐块 flush
    let body_stream = futures::stream::unfold(rx, |mut rx| async move {
        loop {
            match rx.recv().await {
                Some(StreamEvent::Chunk(b)) => {
                    return Some((Ok::<Bytes, std::io::Error>(Bytes::from(b)), rx))
                }
                Some(StreamEvent::End) | None => return None,
                Some(StreamEvent::Abort(_)) => return None,
                Some(StreamEvent::Head { .. }) => continue,
            }
        }
    });

    let status = StatusCode::from_u16(status).unwrap_or(StatusCode::BAD_GATEWAY);
    (status, hm, Body::from_stream(body_stream)).into_response()
}
