use serde::{Deserialize, Serialize};
use std::collections::HashMap;

/// 客户端在注册时声明的一条隧道（v1 只做 http）
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TunnelDef {
    pub tunnel_id: String,
    /// 子域名路由，如 "app1" 对应 app1.frp.xxx.com
    #[serde(default)]
    pub subdomain: Option<String>,
    /// 子路径路由，如 "/u/alice/api" 对应 frp.xxx.com/u/alice/api/...
    #[serde(default)]
    pub path_prefix: Option<String>,
}

/// C -> S
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum ClientMsg {
    Register {
        client_id: String,
        tunnels: Vec<TunnelDef>,
    },
    Pong,
    Response {
        stream_id: u64,
        status: u16,
        #[serde(default)]
        headers: HashMap<String, String>,
        #[serde(default)]
        body_b64: String,
    },
}

/// S -> C
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum ServerMsg {
    RegisterAck { ok: bool, error: Option<String> },
    Ping,
    OpenStream {
        stream_id: u64,
        tunnel_id: String,
        method: String,
        path: String,
        #[serde(default)]
        headers: HashMap<String, String>,
        #[serde(default)]
        body_b64: String,
    },
}

#[derive(Debug, Clone)]
pub struct LocalResponse {
    pub status: u16,
    pub headers: HashMap<String, String>,
    pub body: Vec<u8>,
}
