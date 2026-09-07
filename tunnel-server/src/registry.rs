use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::{mpsc, oneshot, RwLock};

use crate::protocol::{LocalResponse, ServerMsg};

#[derive(Debug, Clone)]
pub struct RouteTarget {
    pub client_id: String,
    pub tunnel_id: String,
    /// 如果是子路由隧道，转发时要 strip 的前缀
    pub strip_prefix: Option<String>,
}

#[derive(Debug, Default)]
struct Inner {
    by_subdomain: HashMap<String, RouteTarget>,
    /// (prefix, target)，按 prefix 长度倒序匹配
    by_path: Vec<(String, RouteTarget)>,
    clients: HashMap<String, mpsc::UnboundedSender<ServerMsg>>,
    pending: HashMap<u64, oneshot::Sender<LocalResponse>>,
    next_stream_id: u64,
    /// client_id -> 它注册的路由 keys，用于掉线/重注册时清理
    owned: HashMap<String, Vec<RouteKey>>,
}

#[derive(Debug, Clone)]
enum RouteKey {
    Sub(String),
    Path(String),
}

#[derive(Clone)]
pub struct Registry {
    inner: Arc<RwLock<Inner>>,
}

impl Registry {
    pub fn new() -> Self {
        Self {
            inner: Arc::new(RwLock::new(Inner::default())),
        }
    }

    fn normalize_prefix(p: &str) -> String {
        let mut s = p.trim().to_string();
        if !s.starts_with('/') {
            s = format!("/{s}");
        }
        // 去掉尾部 /（根 "/" 除外），匹配时统一处理
        while s.len() > 1 && s.ends_with('/') {
            s.pop();
        }
        s
    }

    /// 注册/覆盖一个 client 的全部隧道
    pub async fn register_client(
        &self,
        client_id: &str,
        sender: mpsc::UnboundedSender<ServerMsg>,
        tunnels: Vec<crate::protocol::TunnelDef>,
    ) -> Result<(), String> {
        // 先校验
        for t in &tunnels {
            if t.subdomain.is_none() && t.path_prefix.is_none() {
                return Err(format!("tunnel {} 必须有 subdomain 或 path_prefix", t.tunnel_id));
            }
            if let Some(sub) = &t.subdomain {
                if sub.contains('.') || sub.contains('/') || sub.is_empty() {
                    return Err(format!("tunnel {} subdomain 非法: {sub}", t.tunnel_id));
                }
            }
        }

        let mut g = self.inner.write().await;
        // 清掉旧路由
        if let Some(keys) = g.owned.remove(client_id) {
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
        g.clients.insert(client_id.to_string(), sender);

        let mut keys = Vec::new();
        for t in tunnels {
            let target = RouteTarget {
                client_id: client_id.to_string(),
                tunnel_id: t.tunnel_id.clone(),
                strip_prefix: t.path_prefix.clone().map(|p| Self::normalize_prefix(&p)),
            };
            if let Some(sub) = t.subdomain {
                let sub = sub.to_lowercase();
                // 简单冲突处理：后注册覆盖
                g.by_subdomain.insert(sub.clone(), target.clone());
                keys.push(RouteKey::Sub(sub));
            }
            if let Some(p) = t.path_prefix {
                let p = Self::normalize_prefix(&p);
                g.by_path.retain(|(prefix, _)| prefix != &p);
                g.by_path.push((p.clone(), target));
                keys.push(RouteKey::Path(p));
            }
        }
        // 长前缀优先
        g.by_path.sort_by(|a, b| b.0.len().cmp(&a.0.len()));
        g.owned.insert(client_id.to_string(), keys);
        Ok(())
    }

    pub async fn remove_client(&self, client_id: &str) {
        let mut g = self.inner.write().await;
        if let Some(keys) = g.owned.remove(client_id) {
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
        g.clients.remove(client_id);
    }

    /// 按 Host + Path 找路由。返回 (target, 转发给内网的 path)
    pub async fn lookup(&self, host: &str, path: &str, base_domain: &str) -> Option<(RouteTarget, String)> {
        let g = self.inner.read().await;
        let host = host.split(':').next().unwrap_or(host).to_lowercase();
        let base = base_domain.to_lowercase();

        // 1. 子域名优先：{sub}.frp.xxx.com
        if host != base && host.ends_with(&format!(".{base}")) {
            let sub = host.trim_end_matches(&format!(".{base}")).to_string();
            if let Some(t) = g.by_subdomain.get(&sub) {
                return Some((t.clone(), path.to_string()));
            }
            return None;
        }

        // 2. 主域名 + 子路由：frp.xxx.com/u/alice/api/...
        if host == base {
            for (prefix, target) in &g.by_path {
                if path == prefix || path.starts_with(&format!("{prefix}/")) {
                    // strip prefix，本地服务看到的是 /...
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

    pub async fn client_sender(&self, client_id: &str) -> Option<mpsc::UnboundedSender<ServerMsg>> {
        self.inner.read().await.clients.get(client_id).cloned()
    }

    pub async fn next_stream_id(&self) -> u64 {
        let mut g = self.inner.write().await;
        g.next_stream_id += 1;
        g.next_stream_id
    }

    pub async fn insert_pending(&self, id: u64, tx: oneshot::Sender<LocalResponse>) {
        self.inner.write().await.pending.insert(id, tx);
    }

    pub async fn take_pending(&self, id: u64) -> Option<oneshot::Sender<LocalResponse>> {
        self.inner.write().await.pending.remove(&id)
    }
}
