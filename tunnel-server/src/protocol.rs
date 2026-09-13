use serde::{Deserialize, Serialize};
use std::collections::HashMap;

/// 客户端上报的单条隧道（即 models::TunnelDef 的线上表示）
pub use crate::models::TunnelDef;

/// 服务端下发的“生效隧道”（含命名空间后的公网地址）
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EffectiveTunnel {
    pub tunnel_id: String,
    pub proto: String,
    pub public_url: String,
    pub public_port: Option<u16>,
}

/// C -> S
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum ClientMsg {
    Register {
        #[serde(default)]
        client_id: Option<String>,
        tunnels: Vec<TunnelDef>,
    },
    Pong,
    /// 响应头（流式第一步）
    ResponseHead {
        stream_id: u64,
        status: u16,
        #[serde(default)]
        headers: HashMap<String, String>,
    },
    /// 响应体分块（可多次）
    Chunk {
        stream_id: u64,
        #[serde(default)]
        data_b64: String,
    },
    /// 响应结束
    End {
        stream_id: u64,
    },
    /// 客户端主动报错结束
    Abort {
        stream_id: u64,
        reason: String,
    },
}

/// S -> C
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum ServerMsg {
    RegisterAck {
        ok: bool,
        error: Option<String>,
        #[serde(default)]
        tunnels: Vec<EffectiveTunnel>,
    },
    Ping,
    OpenStream {
        stream_id: u64,
        tunnel_id: String,
        proto: String,
        #[serde(default)]
        method: String,
        #[serde(default)]
        path: String,
        #[serde(default)]
        headers: HashMap<String, String>,
        #[serde(default)]
        body_b64: String,
    },
    /// 通知客户端取消（访客断开）
    CloseStream {
        stream_id: u64,
    },
    /// 服务端 -> 客户端的上行数据（TCP 双向 / 后续流式上传）
    Chunk {
        stream_id: u64,
        #[serde(default)]
        data_b64: String,
    },
}

/// 服务端内部：一次访客流的客户端回包事件
#[derive(Debug)]
pub enum StreamEvent {
    Head {
        status: u16,
        headers: HashMap<String, String>,
    },
    Chunk(Vec<u8>),
    End,
    Abort(String),
}
