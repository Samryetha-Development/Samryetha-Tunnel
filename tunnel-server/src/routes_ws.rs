use axum::extract::ws::{Message, WebSocket, WebSocketUpgrade};
use axum::extract::{Query, State};
use axum::http::{HeaderMap, StatusCode};
use axum::response::{IntoResponse, Response};
use futures::{SinkExt, StreamExt};
use std::collections::HashMap;
use std::time::Duration;
use tokio::sync::mpsc;
use tracing::{info, warn};

use crate::auth::{self, AppState};
use crate::models::{TunnelRow, User};
use crate::protocol::{ClientMsg, EffectiveTunnel, ServerMsg, StreamEvent};
use crate::registry::RouteInput;
use crate::util;

pub async fn tunnel_handler(
    State(state): State<AppState>,
    Query(q): Query<HashMap<String, String>>,
    headers: HeaderMap,
    ws: WebSocketUpgrade,
) -> Response {
    let Some(raw) = util::parse_bearer(&headers) else {
        return (StatusCode::UNAUTHORIZED, "缺少 Bearer Token").into_response();
    };
    let Some(user) = auth::api_token_user(&state.db, &raw).await else {
        return (StatusCode::UNAUTHORIZED, "API Token 无效").into_response();
    };
    // v=3 走私有二进制协议；缺省为 JSON 兼容模式
    let binary = q.get("v").map(|v| v == "3").unwrap_or(false);
    // 每条 WebSocket 连接一个 conn_id：一个客户端可开多条连接（每隧道独立，避免队头阻塞）
    let conn_id = state.registry.new_conn_id().await;
    ws.on_upgrade(move |socket| handle_socket(socket, state, user, binary, conn_id))
        .into_response()
}

fn sanitize_name(s: &str) -> String {
    let mut out = String::new();
    for c in s.chars() {
        let c = c.to_ascii_lowercase();
        if c.is_ascii_alphanumeric() || c == '-' {
            out.push(c);
        } else if c == '_' || c == '.' {
            out.push('-');
        }
    }
    out.trim_matches('-').to_string()
}

struct Prepared {
    input: RouteInput,
    public_url: String,
    public_port: Option<u16>,
    disabled: bool,
}

async fn prepare(
    state: &AppState,
    user: &User,
    defs: &[crate::models::TunnelDef],
    conn_id: u64,
) -> (Vec<Prepared>, Vec<String>) {
    let mut prepared = Vec::new();
    let mut errors = Vec::new();
    let slug = user.slug.clone();

    for def in defs {
        let proto = def.proto.clone().unwrap_or_else(|| "http".into());
        let tunnel_id = def.tunnel_id.trim().to_string();
        if tunnel_id.is_empty() {
            errors.push("存在空的 tunnel_id".into());
            continue;
        }

        // 读旧配置（保留 visitor_auth / disabled）
        let existing: Option<TunnelRow> =
            sqlx::query_as("SELECT * FROM tunnels WHERE user_id = ? AND tunnel_id = ?")
                .bind(user.id)
                .bind(&tunnel_id)
                .fetch_optional(&state.db)
                .await
                .unwrap_or(None);
        let disabled = existing.as_ref().map(|r| r.disabled).unwrap_or(false);
        let visitor_auth = existing.as_ref().and_then(|r| r.visitor_auth.clone());

        let (public_host, path_prefix, public_url, public_port);

        if proto == "tcp" {
            // 纯 IP 中转：端口池只在 relay 角色可用
            if !matches!(state.cfg.role, crate::config::Role::Relay) {
                errors.push(format!(
                    "{tunnel_id}: tcp 隧道只能在 relay（纯 IP 中转）角色注册"
                ));
                continue;
            }
            match state
                .registry
                .allocate_port(
                    conn_id,
                    &tunnel_id,
                    state.cfg.tcp_port_start,
                    state.cfg.tcp_port_end,
                )
                .await
            {
                Some(port) => {
                    public_host = format!("{}:{}", state.cfg.relay_host, port);
                    path_prefix = None;
                    public_url = format!("{}:{}", state.cfg.relay_host, port);
                    public_port = Some(port);
                }
                None => {
                    errors.push(format!("{tunnel_id}: TCP 端口池已满"));
                    continue;
                }
            }
        } else {
            public_port = None;
            if let Some(sub) = def.subdomain.as_deref().map(sanitize_name).filter(|s| !s.is_empty()) {
                let eff = format!("{}-{}", slug, sub);
                public_host = eff.clone();
                path_prefix = None;
                public_url = format!("https://{}.{}", eff, state.cfg.base_domain);
            } else {
                let base = format!("{}/{}", state.cfg.path_ns, slug); // PATH_NS 为空时即 /<slug>
                let eff_path = match def.path_prefix.as_deref().map(|s| s.trim()).filter(|s| !s.is_empty()) {
                    Some(p) => {
                        let p = crate::registry::Registry::normalize_prefix(p);
                        if p == base || p.starts_with(&format!("{base}/")) {
                            p
                        } else {
                            format!("{base}{p}")
                        }
                    }
                    None => format!("{base}/{tunnel_id}"),
                };
                public_host = String::new();
                path_prefix = Some(eff_path.clone());
                public_url = format!("https://{}{}", state.cfg.base_domain, eff_path);
            }
        }

        prepared.push(Prepared {
            input: RouteInput {
                user_id: user.id,
                slug: slug.clone(),
                tunnel_id: tunnel_id.clone(),
                proto: proto.clone(),
                effective_sub: if proto == "http" && path_prefix.is_none() {
                    Some(public_host.clone())
                } else {
                    None
                },
                effective_path: path_prefix.clone(),
                visitor_auth: visitor_auth.clone(),
            },
            public_url,
            public_port,
            disabled,
        });
    }
    (prepared, errors)
}

async fn persist(state: &AppState, user: &User, prepared: &[Prepared]) {
    let now = util::now();
    // 注意：多连接模式下不再删除「本次未上报」的旧隧道行
    // （同一用户可能有多条连接，各自只上报自己那部分隧道）。
    // 需要清理请在 Web 控制台/API 删除，未连接的会显示为离线。
    for p in prepared {
        let public_host = if p.input.proto == "tcp" {
            p.public_url.clone()
        } else if let Some(sub) = &p.input.effective_sub {
            sub.clone()
        } else {
            String::new()
        };
        let _ = sqlx::query(
            "INSERT INTO tunnels (user_id, tunnel_id, proto, public_host, path_prefix, local_addr, created_at, updated_at) \
             VALUES (?, ?, ?, ?, ?, ?, ?, ?) \
             ON CONFLICT(user_id, tunnel_id) DO UPDATE SET proto = excluded.proto, public_host = excluded.public_host, \
             path_prefix = excluded.path_prefix, local_addr = excluded.local_addr, updated_at = excluded.updated_at",
        )
        .bind(user.id)
        .bind(&p.input.tunnel_id)
        .bind(&p.input.proto)
        .bind(&public_host)
        .bind(&p.input.effective_path)
        .bind(None::<String>)
        .bind(&now)
        .bind(&now)
        .execute(&state.db)
        .await;
    }
}

fn encode_out(m: &ServerMsg, binary: bool) -> Message {
    if binary {
        Message::Binary(crate::binproto::encode_server(m))
    } else {
        Message::Text(serde_json::to_string(m).unwrap())
    }
}

pub async fn handle_socket(socket: WebSocket, state: AppState, user: User, binary: bool, conn_id: u64) {
    let user_id = user.id;
    let (mut ws_tx, mut ws_rx) = socket.split();
    let (tx, mut rx) = mpsc::unbounded_channel::<ServerMsg>();

    let write_task = tokio::spawn(async move {
        let mut interval = tokio::time::interval(Duration::from_secs(20));
        loop {
            tokio::select! {
                _ = interval.tick() => {
                    if ws_tx.send(encode_out(&ServerMsg::Ping, binary)).await.is_err() {
                        break;
                    }
                }
                msg = rx.recv() => {
                    match msg {
                        Some(m) => {
                            if ws_tx.send(encode_out(&m, binary)).await.is_err() {
                                break;
                            }
                        }
                        None => break,
                    }
                }
            }
        }
    });

    info!(
        "control channel opened: user={} ({}) conn={} proto={}",
        user.email,
        user_id,
        conn_id,
        if binary { "binary-v3" } else { "json" }
    );

    while let Some(msg) = ws_rx.next().await {
        let Ok(msg) = msg else { break };
        let parsed: Result<ClientMsg, String> = match msg {
            Message::Binary(b) if binary => crate::binproto::decode_client(&b),
            Message::Text(t) if !binary => serde_json::from_str(&t).map_err(|e| e.to_string()),
            Message::Close(_) => break,
            _ => continue,
        };
        let Ok(parsed) = parsed else {
            warn!("bad frame from user {user_id}");
            continue;
        };
        match parsed {
            ClientMsg::Register { tunnels, .. } => {
                if tunnels.len() as i64 > user.max_tunnels {
                    let _ = tx.send(ServerMsg::RegisterAck {
                        ok: false,
                        error: Some(format!(
                            "隧道数 {} 超过配额 {}",
                            tunnels.len(),
                            user.max_tunnels
                        )),
                        tunnels: vec![],
                    });
                    continue;
                }
                let (prepared, errors) = {
                    // 先释放旧 TCP 端口，再重新分配
                    state.registry.release_ports(conn_id).await;
                    prepare(&state, &user, &tunnels, conn_id).await
                };
                persist(&state, &user, &prepared).await;

                let inputs: Vec<RouteInput> = prepared
                    .iter()
                    .filter(|p| !p.disabled)
                    .map(|p| RouteInput {
                        user_id: p.input.user_id,
                        slug: p.input.slug.clone(),
                        tunnel_id: p.input.tunnel_id.clone(),
                        proto: p.input.proto.clone(),
                        effective_sub: p.input.effective_sub.clone(),
                        effective_path: p.input.effective_path.clone(),
                        visitor_auth: p.input.visitor_auth.clone(),
                    })
                    .collect();

                let effective: Vec<EffectiveTunnel> = prepared
                    .iter()
                    .map(|p| EffectiveTunnel {
                        tunnel_id: p.input.tunnel_id.clone(),
                        proto: p.input.proto.clone(),
                        public_url: p.public_url.clone(),
                        public_port: p.public_port,
                    })
                    .collect();

                state
                    .registry
                    .register_client(conn_id, user_id, tx.clone(), inputs)
                    .await;
                crate::db::audit(
                    &state.db,
                    Some(&user.email),
                    "tunnel.register",
                    None,
                    Some(&format!("{} 条", effective.len())),
                    None,
                )
                .await;
                info!("user {user_id} registered {} tunnels", effective.len());
                let err = if errors.is_empty() {
                    None
                } else {
                    Some(errors.join("; "))
                };
                let _ = tx.send(ServerMsg::RegisterAck {
                    ok: errors.is_empty() || !effective.is_empty(),
                    error: err,
                    tunnels: effective,
                });
            }
            ClientMsg::Pong => {}
            ClientMsg::ResponseHead {
                stream_id,
                status,
                headers,
            } => {
                state
                    .registry
                    .route_event(stream_id, StreamEvent::Head { status, headers })
                    .await;
            }
            ClientMsg::Chunk { stream_id, data } => {
                if !data.is_empty() {
                    state.usage.add(user_id, data.len() as i64, 0).await;
                }
                state.registry.route_event(stream_id, StreamEvent::Chunk(data)).await;
            }
            ClientMsg::End { stream_id } => {
                state.registry.route_event(stream_id, StreamEvent::End).await;
                state.registry.take_pending(stream_id).await;
                state.stream_release(user_id).await;
            }
            ClientMsg::Abort { stream_id, reason } => {
                state.registry.route_event(stream_id, StreamEvent::Abort(reason)).await;
                state.registry.take_pending(stream_id).await;
                state.stream_release(user_id).await;
            }
        }
    }

    info!("control channel closed: user={user_id}");
    state.registry.remove_client(conn_id).await;
    write_task.abort();
    let _ = write_task.await;
}

pub fn filter_headers(
    input: &std::collections::HashMap<String, String>,
) -> std::collections::HashMap<String, String> {
    let skip = ["connection", "keep-alive", "transfer-encoding", "upgrade", "content-length", "host"];
    input
        .iter()
        .filter(|(k, _)| !skip.contains(&k.to_lowercase().as_str()))
        .map(|(k, v)| (k.clone(), v.clone()))
        .collect()
}
