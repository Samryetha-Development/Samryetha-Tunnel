use axum::{
    body::Bytes,
    extract::State,
    http::{HeaderMap, StatusCode},
    response::{IntoResponse, Response},
};
use base64::Engine;
use std::collections::HashMap;
use std::time::Duration;
use tracing::warn;

use crate::protocol::ServerMsg;
use crate::AppState;

/// 访客流量入口：所有非 /healthz /tunnel 的请求都到这里
pub async fn visitor_handler(
    State(state): State<AppState>,
    req: axum::extract::Request,
) -> Response {
    let host = req
        .headers()
        .get("host")
        .and_then(|v| v.to_str().ok())
        .unwrap_or("")
        .to_string();
    let method = req.method().to_string();
    let full_path = req
        .uri()
        .path_and_query()
        .map(|x| x.to_string())
        .unwrap_or_else(|| "/".to_string());

    // 路由查找（注意：lookup 内部会对子路由做 strip）
    let found = state
        .registry
        .lookup(&host, req.uri().path(), &state.base_domain)
        .await;
    let (target, forward_path) = match found {
        Some(v) => v,
        None => {
            return (StatusCode::NOT_FOUND, "no such tunnel (check subdomain/path)").into_response();
        }
    };

    // query 要拼回 forward_path
    let forward_full = if let Some(q) = req.uri().query() {
        // forward_path 本身不含 query（lookup 只给了 path），这里拼上
        if forward_path.contains('?') {
            format!("{forward_path}&{q}")
        } else {
            format!("{forward_path}?{q}")
        }
    } else {
        forward_path.clone()
    };
    let _ = full_path; // 保留原始 path 用于日志即可

    // 收集请求头 + body（v1 限 5MB）
    let mut headers = HashMap::new();
    for (k, v) in req.headers().iter() {
        if let Ok(vs) = v.to_str() {
            headers.insert(k.to_string(), vs.to_string());
        }
    }
    // 子路由：告诉内网服务原始前缀，支持 basePath 的框架可用
    if let Some(strip) = &target.strip_prefix {
        headers.insert("x-forwarded-prefix".to_string(), strip.clone());
        headers.insert("x-forwarded-host".to_string(), host.clone());
    }

    let body_bytes: Bytes = match axum::body::to_bytes(req.into_body(), 5 * 1024 * 1024).await {
        Ok(b) => b,
        Err(_) => return (StatusCode::PAYLOAD_TOO_LARGE, "body > 5MB (v1 limit)").into_response(),
    };

    let sender = match state.registry.client_sender(&target.client_id).await {
        Some(s) => s,
        None => return (StatusCode::BAD_GATEWAY, "client offline").into_response(),
    };

    let stream_id = state.registry.next_stream_id().await;
    let (tx, rx) = tokio::sync::oneshot::channel();
    state.registry.insert_pending(stream_id, tx).await;

    let open = ServerMsg::OpenStream {
        stream_id,
        tunnel_id: target.tunnel_id.clone(),
        method,
        path: forward_full,
        headers: crate::control::filter_headers(&headers),
        body_b64: base64::engine::general_purpose::STANDARD.encode(&body_bytes),
    };
    if sender.send(open).is_err() {
        state.registry.take_pending(stream_id).await;
        return (StatusCode::BAD_GATEWAY, "client gone").into_response();
    }

    // 等客户端回包，30s 超时
    let resp = match tokio::time::timeout(Duration::from_secs(30), rx).await {
        Ok(Ok(r)) => r,
        _ => {
            state.registry.take_pending(stream_id).await;
            warn!("tunnel timeout stream={stream_id}");
            return (StatusCode::GATEWAY_TIMEOUT, "tunnel timeout (local service slow?)")
                .into_response();
        }
    };

    let mut out = HeaderMap::new();
    for (k, v) in resp.headers {
        // 过滤非法头，防止 panic
        if let (Ok(hn), Ok(hv)) = (
            axum::http::HeaderName::try_from(k.to_lowercase()),
            axum::http::HeaderValue::from_str(&v),
        ) {
            // content-length 由 body 自动定，跳过以免冲突
            if hn == "content-length" {
                continue;
            }
            out.insert(hn, hv);
        }
    }
    let status = StatusCode::from_u16(resp.status).unwrap_or(StatusCode::BAD_GATEWAY);
    (status, out, resp.body).into_response()
}
