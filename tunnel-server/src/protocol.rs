use serde::{Deserialize, Serialize};
use std::collections::HashMap;

/// 客户端上报的单条隧道（即 models::TunnelDef 的线上表示）
pub use crate::models::TunnelDef;

/// base64 编解码（仅 JSON 兼容模式使用；二进制模式走原始字节）
pub mod b64serde {
    use base64::Engine;
    use serde::{Deserialize, Deserializer, Serializer};

    pub fn serialize<S: Serializer>(v: &[u8], s: S) -> Result<S::Ok, S::Error> {
        s.serialize_str(&base64::engine::general_purpose::STANDARD.encode(v))
    }

    pub fn deserialize<'de, D: Deserializer<'de>>(d: D) -> Result<Vec<u8>, D::Error> {
        let s = String::deserialize(d)?;
        base64::engine::general_purpose::STANDARD
            .decode(s.as_bytes())
            .map_err(serde::de::Error::custom)
    }
}

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
    ResponseHead {
        stream_id: u64,
        status: u16,
        #[serde(default)]
        headers: HashMap<String, String>,
    },
    Chunk {
        stream_id: u64,
        #[serde(rename = "data_b64", with = "b64serde", default)]
        data: Vec<u8>,
    },
    End {
        stream_id: u64,
    },
    Abort {
        stream_id: u64,
        reason: String,
    },
    /// 管理请求（走同一条控制通道，无需公网管理 API）
    Mgmt {
        req_id: u64,
        method: String,
        path: String,
        #[serde(rename = "body_b64", with = "b64serde", default)]
        body: Vec<u8>,
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
        #[serde(rename = "body_b64", with = "b64serde", default)]
        body: Vec<u8>,
    },
    CloseStream {
        stream_id: u64,
    },
    Chunk {
        stream_id: u64,
        #[serde(rename = "data_b64", with = "b64serde", default)]
        data: Vec<u8>,
    },
    /// 管理响应
    MgmtResp {
        req_id: u64,
        status: u16,
        #[serde(rename = "body_b64", with = "b64serde", default)]
        body: Vec<u8>,
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
