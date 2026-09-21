use crate::auth::AppState;
use crate::protocol::{ServerMsg, StreamEvent};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpListener;
use tokio::sync::Mutex;
use tokio::task::JoinHandle;
use tracing::{info, warn};

/// 纯 IP 中转：按注册的 TCP 端口开公网监听，把字节经控制通道转发给客户端
pub async fn run(state: AppState) {
    let listeners: Arc<Mutex<HashMap<(i64, String, u16), JoinHandle<()>>>> =
        Arc::new(Mutex::new(HashMap::new()));

    loop {
        let bindings: HashMap<(i64, String, u16), ()> = state            .registry
            .tcp_bindings()
            .await
            .into_iter()
            .map(|(u, t, p)| ((u, t, p), ()))
            .collect();

        let mut guard = listeners.lock().await;
        // 停掉已失效的监听
        let stale: Vec<_> = guard
            .keys()
            .filter(|k| !bindings.contains_key(k))
            .cloned()
            .collect();
        for k in stale {
            if let Some(h) = guard.remove(&k) {
                h.abort();
                info!("relay: 关闭端口 {}", k.2);
            }
        }
        // 启动新监听
        for (uid, tid, port) in bindings.keys() {
            let key = (*uid, tid.clone(), *port);
            if guard.contains_key(&key) {
                continue;
            }
            match TcpListener::bind(("0.0.0.0", *port)).await {
                Ok(listener) => {
                    info!("relay: 监听 0.0.0.0:{port} -> user {uid} / {tid}");
                    let st = state.clone();
                    let uid2 = *uid;
                    let tid2 = tid.clone();
                    let handle = tokio::spawn(async move {
                        loop {
                            match listener.accept().await {
                                Ok((sock, peer)) => {
                                    let st2 = st.clone();
                                    let tid3 = tid2.clone();
                                    tokio::spawn(async move {
                                        handle_conn(st2, uid2, tid3, sock, peer).await;
                                    });
                                }
                                Err(e) => {
                                    warn!("relay accept 失败: {e}");
                                    break;
                                }
                            }
                        }
                    });
                    guard.insert(key, handle);
                }
                Err(e) => {
                    warn!("relay 绑定端口 {port} 失败: {e}");
                }
            }
        }
        drop(guard);
        tokio::time::sleep(std::time::Duration::from_secs(2)).await;
    }
}

async fn handle_conn(
    state: AppState,
    user_id: i64,
    tunnel_id: String,
    stream: tokio::net::TcpStream,
    peer: std::net::SocketAddr,
) {
    if !state.stream_acquire(user_id).await {
        warn!("relay: user {user_id} 并发已达上限，拒绝 {peer}");
        return;
    }
    let stream_id = state.registry.next_stream_id().await;
    let (tx, mut rx) = tokio::sync::mpsc::unbounded_channel::<StreamEvent>();
    state.registry.insert_pending(stream_id, tx).await;

    let open = ServerMsg::OpenStream {
        stream_id,
        tunnel_id: tunnel_id.clone(),
        proto: "tcp".into(),
        method: String::new(),
        path: String::new(),
        headers: Default::default(),
        body: Vec::new(),
    };
    if !state.registry.send_to_user(user_id, open).await {
        state.registry.take_pending(stream_id).await;
        state.stream_release(user_id).await;
        return;
    }
    state.usage.add(user_id, 0, 1).await;

    let (mut rd, mut wr) = stream.into_split();

    // 客户端 -> 访客
    let writer = tokio::spawn(async move {
        while let Some(ev) = rx.recv().await {
            match ev {
                StreamEvent::Chunk(b) => {
                    if wr.write_all(&b).await.is_err() {
                        break;
                    }
                }
                StreamEvent::End | StreamEvent::Abort(_) => break,
                StreamEvent::Head { .. } => {}
            }
        }
        let _ = wr.shutdown().await;
    });

    // 访客 -> 客户端
    let mut buf = vec![0u8; 32 * 1024];
    loop {
        match rd.read(&mut buf).await {
            Ok(0) => break,
            Ok(n) => {
                state.usage.add(user_id, n as i64, 0).await;
                state
                    .registry
                    .send_to_user(
                        user_id,
                        ServerMsg::Chunk {
                            stream_id,
                            data: buf[..n].to_vec(),
                        },
                    )
                    .await;
            }
            Err(_) => break,
        }
    }

    // 访客断开：通知客户端关闭本地连接
    state
        .registry
        .send_to_user(user_id, ServerMsg::CloseStream { stream_id })
        .await;
    let _ = writer.await;
    state.registry.take_pending(stream_id).await;
    state.stream_release(user_id).await;
    info!("relay: 连接结束 {peer} -> {tunnel_id}");
}
