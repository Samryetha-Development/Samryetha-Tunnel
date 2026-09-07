use axum::extract::ws::{Message, WebSocket};
use futures::{SinkExt, StreamExt};
use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::mpsc;
use tracing::{info, warn};

use crate::protocol::{ClientMsg, LocalResponse, ServerMsg};
use crate::registry::Registry;
use base64::Engine;

pub async fn handle_socket(socket: WebSocket, registry: Registry) {
    let (mut ws_tx, mut ws_rx) = socket.split();
    let (tx, mut rx) = mpsc::unbounded_channel::<ServerMsg>();

    // 写回任务：registry -> WS
    let write_task = tokio::spawn(async move {
        // 保活：25s ping
        let mut interval = tokio::time::interval(Duration::from_secs(25));
        loop {
            tokio::select! {
                _ = interval.tick() => {
                    let ping = serde_json::to_string(&ServerMsg::Ping).unwrap();
                    if ws_tx.send(Message::Text(ping)).await.is_err() {
                        break;
                    }
                }
                msg = rx.recv() => {
                    match msg {
                        Some(m) => {
                            let s = serde_json::to_string(&m).unwrap();
                            if ws_tx.send(Message::Text(s)).await.is_err() {
                                break;
                            }
                        }
                        None => break,
                    }
                }
            }
        }
    });

    let registry2 = registry.clone();
    let mut client_id: Option<String> = None;

    // 读任务：WS -> registry（在当前 task 直接跑）
    while let Some(msg) = ws_rx.next().await {
        let msg = match msg {
            Ok(m) => m,
            Err(_) => break,
        };
        let text = match msg {
            Message::Text(t) => t,
            Message::Close(_) => break,
            // v1 只用 Text JSON，忽略 Binary/Ping
            _ => continue,
        };
        let parsed: Result<ClientMsg, _> = serde_json::from_str(&text);
        let parsed = match parsed {
            Ok(p) => p,
            Err(e) => {
                warn!("bad client frame: {e}");
                continue;
            }
        };
        match parsed {
            ClientMsg::Register { client_id: cid, tunnels } => {
                match registry2.register_client(&cid, tx.clone(), tunnels).await {
                    Ok(()) => {
                        info!("client registered: {cid}");
                        client_id = Some(cid.clone());
                        let ack = ServerMsg::RegisterAck { ok: true, error: None };
                        let _ = tx.send(ack);
                    }
                    Err(e) => {
                        let ack = ServerMsg::RegisterAck { ok: false, error: Some(e) };
                        let _ = tx.send(ack);
                    }
                }
            }
            ClientMsg::Pong => {
                // 保活用，暂不记状态
            }
            ClientMsg::Response { stream_id, status, headers, body_b64 } => {
                if let Some(tx_oneshot) = registry2.take_pending(stream_id).await {
                    let body = base64::engine::general_purpose::STANDARD
                        .decode(body_b64.as_bytes())
                        .unwrap_or_default();
                    // 限制回包 8MB，防止访客被大包打爆
                    let body = if body.len() > 8 * 1024 * 1024 {
                        warn!("response too large, truncated: {stream_id}");
                        body[..8 * 1024 * 1024].to_vec()
                    } else {
                        body
                    };
                    let _ = tx_oneshot.send(LocalResponse { status, headers, body });
                }
            }
        }
    }

    if let Some(cid) = client_id {
        info!("client disconnected: {cid}");
        registry2.remove_client(&cid).await;
    }
    write_task.abort();
    let _ = write_task.await;
}

/// 访客 HTTP 分发用的辅助：把 HashMap headers 过滤 hop-by-hop
pub fn filter_headers(input: &HashMap<String, String>) -> HashMap<String, String> {
    let skip = ["connection", "keep-alive", "transfer-encoding", "upgrade"];
    input
        .iter()
        .filter(|(k, _)| !skip.contains(&k.to_lowercase().as_str()))
        .map(|(k, v)| (k.clone(), v.clone()))
        .collect()
}

pub fn _unused(_: Arc<()>) {}
