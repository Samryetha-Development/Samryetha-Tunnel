use std::collections::{HashMap, HashSet};
use std::sync::Arc;
use tokio::sync::{mpsc, RwLock};

use crate::protocol::{ServerMsg, StreamEvent};

/// 路由目标：指向某条 **连接** 上的某条隧道
#[derive(Debug, Clone)]
#[allow(dead_code)]
pub struct RouteTarget {
    pub conn_id: u64,
    pub user_id: i64,
    pub slug: String,
    pub tunnel_id: String,
    pub proto: String,
    pub strip_prefix: Option<String>,
    pub visitor_auth: Option<String>,
}

#[derive(Debug, Clone)]
pub enum RouteKey {
    Sub(String),
    Path(String),
}

#[derive(Debug, Default)]
struct Inner {
    by_subdomain: HashMap<String, RouteTarget>,
    by_path: Vec<(String, RouteTarget)>,
    /// conn_id -> 发送端
    clients: HashMap<u64, mpsc::UnboundedSender<ServerMsg>>,
    /// conn_id -> user_id
    conn_user: HashMap<u64, i64>,
    pending: HashMap<u64, mpsc::UnboundedSender<StreamEvent>>,
    next_stream_id: u64,
    next_conn_id: u64,
    owned: HashMap<u64, Vec<RouteKey>>,
    /// (conn_id, tunnel_id) -> 公网端口
    tcp_ports: HashMap<(u64, String), u16>,
    used_ports: HashSet<u16>,
}

#[derive(Clone)]
pub struct Registry {
    inner: Arc<RwLock<Inner>>,
}

pub struct RouteInput {
    pub user_id: i64,
    pub slug: String,
    pub tunnel_id: String,
    pub proto: String,
    pub effective_sub: Option<String>,
    pub effective_path: Option<String>,
    pub visitor_auth: Option<String>,
}

impl Registry {
    pub fn new() -> Self {
        Self {
            inner: Arc::new(RwLock::new(Inner::default())),
        }
    }

    /// 分配一个新的连接 ID（一个客户端可开多条连接）
    pub async fn new_conn_id(&self) -> u64 {
        let mut g = self.inner.write().await;
        g.next_conn_id += 1;
        g.next_conn_id
    }

    pub fn normalize_prefix(p: &str) -> String {
        let mut s = p.trim().to_string();
        if !s.starts_with('/') {
            s = format!("/{s}");
        }
        while s.len() > 1 && s.ends_with('/') {
            s.pop();
        }
        s
    }

    pub async fn register_client(
        &self,
        conn_id: u64,
        user_id: i64,
        sender: mpsc::UnboundedSender<ServerMsg>,
        inputs: Vec<RouteInput>,
    ) -> Vec<RouteKey> {
        let mut g = self.inner.write().await;
        // 清旧路由
        if let Some(keys) = g.owned.remove(&conn_id) {
            for k in keys {
                match k {
                    RouteKey::Sub(s) => {
                        g.by_subdomain.remove(&s);
                    }
                    RouteKey::Path(p) => {
                        g.by_path.retain(|(prefix, _)| prefix != &p);
                    }
                }
            }
        }
        g.clients.insert(conn_id, sender);
        g.conn_user.insert(conn_id, user_id);

        let mut keys = Vec::new();
        for input in inputs {
            let target = RouteTarget {
                conn_id,
                user_id: input.user_id,
                slug: input.slug.clone(),
                tunnel_id: input.tunnel_id.clone(),
                proto: input.proto.clone(),
                strip_prefix: input
                    .effective_path
                    .as_ref()
                    .map(|p| Self::normalize_prefix(p)),
                visitor_auth: input.visitor_auth,
            };
            if let Some(sub) = input.effective_sub {
                let sub = sub.to_lowercase();
                g.by_subdomain.insert(sub.clone(), target.clone());
                keys.push(RouteKey::Sub(sub));
            }
            if let Some(p) = input.effective_path {
                let p = Self::normalize_prefix(&p);
                g.by_path.retain(|(prefix, _)| prefix != &p);
                g.by_path.push((p.clone(), target));
                keys.push(RouteKey::Path(p));
            }
        }
        g.by_path.sort_by(|a, b| b.0.len().cmp(&a.0.len()));
        g.owned.insert(conn_id, keys.clone());
        keys
    }

    pub async fn remove_client(&self, conn_id: u64) {
        let mut g = self.inner.write().await;
        if let Some(keys) = g.owned.remove(&conn_id) {
            for k in keys {
                match k {
                    RouteKey::Sub(s) => {
                        g.by_subdomain.remove(&s);
                    }
                    RouteKey::Path(p) => {
                        g.by_path.retain(|(prefix, _)| prefix != &p);
                    }
                }
            }
        }
        let ports: Vec<(u64, String)> = g
            .tcp_ports
            .keys()
            .filter(|(cid, _)| *cid == conn_id)
            .cloned()
            .collect();
        for k in ports {
            if let Some(p) = g.tcp_ports.remove(&k) {
                g.used_ports.remove(&p);
            }
        }
        g.clients.remove(&conn_id);
        g.conn_user.remove(&conn_id);
    }

    pub async fn allocate_port(
        &self,
        conn_id: u64,
        tunnel_id: &str,
        start: u16,
        end: u16,
    ) -> Option<u16> {
        let mut g = self.inner.write().await;
        for p in start..=end {
            if !g.used_ports.contains(&p) {
                g.used_ports.insert(p);
                g.tcp_ports.insert((conn_id, tunnel_id.to_string()), p);
                return Some(p);
            }
        }
        None
    }

    pub async fn release_ports(&self, conn_id: u64) {
        let mut g = self.inner.write().await;
        let keys: Vec<(u64, String)> = g
            .tcp_ports
            .keys()
            .filter(|(cid, _)| *cid == conn_id)
            .cloned()
            .collect();
        for k in keys {
            if let Some(p) = g.tcp_ports.remove(&k) {
                g.used_ports.remove(&p);
            }
        }
    }

    pub async fn lookup(&self, host: &str, path: &str, base_domain: &str) -> Option<(RouteTarget, String)> {
        let g = self.inner.read().await;
        let host = host.split(':').next().unwrap_or(host).to_lowercase();
        let base = base_domain.to_lowercase();

        if host != base && host.ends_with(&format!(".{base}")) {
            let sub = host.trim_end_matches(&format!(".{base}")).to_string();
            if let Some(t) = g.by_subdomain.get(&sub) {
                return Some((t.clone(), path.to_string()));
            }
            return None;
        }

        if host == base {
            for (prefix, target) in &g.by_path {
                if path == prefix || path.starts_with(&format!("{prefix}/")) {
                    let mut rest = path[prefix.len()..].to_string();
                    if rest.is_empty() {
                        rest = "/".to_string();
                    }
                    return Some((target.clone(), rest));
                }
            }
        }
        None
    }

    /// 向指定连接发送消息
    pub async fn send_to_conn(&self, conn_id: u64, msg: ServerMsg) -> bool {
        let g = self.inner.read().await;
        g.clients
            .get(&conn_id)
            .map(|s| s.send(msg).is_ok())
            .unwrap_or(false)
    }

    pub async fn next_stream_id(&self) -> u64 {
        let mut g = self.inner.write().await;
        g.next_stream_id += 1;
        g.next_stream_id
    }

    pub async fn insert_pending(&self, id: u64, tx: mpsc::UnboundedSender<StreamEvent>) {
        self.inner.write().await.pending.insert(id, tx);
    }

    pub async fn take_pending(&self, id: u64) -> Option<mpsc::UnboundedSender<StreamEvent>> {
        self.inner.write().await.pending.remove(&id)
    }

    pub async fn route_event(&self, id: u64, ev: StreamEvent) -> bool {
        let g = self.inner.read().await;
        g.pending.get(&id).map(|tx| tx.send(ev).is_ok()).unwrap_or(false)
    }

    /// 断开某用户的全部连接（管理员停用用户时用）
    pub async fn remove_user_conns(&self, user_id: i64) {
        let conns: Vec<u64> = {
            let g = self.inner.read().await;
            g.conn_user
                .iter()
                .filter(|(_, uid)| **uid == user_id)
                .map(|(cid, _)| *cid)
                .collect()
        };
        for cid in conns {
            self.remove_client(cid).await;
        }
    }

    /// 去重后的在线用户
    pub async fn online_users(&self) -> Vec<i64> {
        let g = self.inner.read().await;
        let mut v: Vec<i64> = g.conn_user.values().cloned().collect();
        v.sort_unstable();
        v.dedup();
        v
    }

    /// relay 用：当前所有 TCP 端口绑定 (conn_id, user_id, tunnel_id, port)
    pub async fn tcp_bindings(&self) -> Vec<(u64, i64, String, u16)> {
        let g = self.inner.read().await;
        g.tcp_ports
            .iter()
            .map(|((cid, tid), port)| {
                let user = g.conn_user.get(cid).cloned().unwrap_or(0);
                (*cid, user, tid.clone(), *port)
            })
            .collect()
    }
}
