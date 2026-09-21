//! tunnel 二进制线格式（v3）—— 私有协议，替代 JSON + base64
//!
//! 每一帧 = 一个 WebSocket 二进制消息。字段手工编码，无反射、无字符串键。
//! 目的：更小的字节数、更少的分配与 CPU，从而降低延迟与抖动。
//!
//! 编码约定：
//!   varint : LEB128 无符号
//!   str    : varint 长度 + UTF-8 字节
//!   bytes  : varint 长度 + 原始字节
//!   u8/u16 : 小端定长

use crate::models::TunnelDef;
use crate::protocol::{ClientMsg, ServerMsg};
use std::collections::HashMap;

pub const T_REGISTER: u8 = 0x01;
pub const T_REGISTER_ACK: u8 = 0x02;
pub const T_PING: u8 = 0x03;
pub const T_PONG: u8 = 0x04;
pub const T_OPEN_STREAM: u8 = 0x05;
pub const T_RESPONSE_HEAD: u8 = 0x06;
pub const T_CHUNK: u8 = 0x07;
pub const T_END: u8 = 0x08;
pub const T_ABORT: u8 = 0x09;
pub const T_CLOSE_STREAM: u8 = 0x0A;

fn proto_to_u8(p: &str) -> u8 {
    if p == "tcp" {
        1
    } else {
        0
    }
}

fn u8_to_proto(b: u8) -> String {
    if b == 1 {
        "tcp".to_string()
    } else {
        "http".to_string()
    }
}

// ---------------- 写 ----------------

pub struct W {
    pub buf: Vec<u8>,
}

impl W {
    pub fn new() -> Self {
        Self {
            buf: Vec::with_capacity(256),
        }
    }
    #[inline]
    pub fn u8(&mut self, v: u8) {
        self.buf.push(v);
    }
    #[inline]
    pub fn u16(&mut self, v: u16) {
        self.buf.extend_from_slice(&v.to_le_bytes());
    }
    #[inline]
    pub fn varint(&mut self, mut v: u64) {
        while v >= 0x80 {
            self.buf.push((v as u8) | 0x80);
            v >>= 7;
        }
        self.buf.push(v as u8);
    }
    #[inline]
    pub fn str(&mut self, s: &str) {
        self.varint(s.len() as u64);
        self.buf.extend_from_slice(s.as_bytes());
    }
    #[inline]
    pub fn bytes(&mut self, b: &[u8]) {
        self.varint(b.len() as u64);
        self.buf.extend_from_slice(b);
    }
    fn headers(&mut self, h: &HashMap<String, String>) {
        self.u16(h.len() as u16);
        for (k, v) in h {
            self.str(k);
            self.str(v);
        }
    }
}

// ---------------- 读 ----------------

pub struct R<'a> {
    d: &'a [u8],
    i: usize,
}

impl<'a> R<'a> {
    pub fn new(d: &'a [u8]) -> Self {
        Self { d, i: 0 }
    }
    fn need(&self, n: usize) -> Result<(), String> {
        if self.i + n > self.d.len() {
            Err("帧被截断".into())
        } else {
            Ok(())
        }
    }
    #[inline]
    pub fn u8(&mut self) -> Result<u8, String> {
        self.need(1)?;
        let v = self.d[self.i];
        self.i += 1;
        Ok(v)
    }
    #[inline]
    pub fn u16(&mut self) -> Result<u16, String> {
        self.need(2)?;
        let v = u16::from_le_bytes([self.d[self.i], self.d[self.i + 1]]);
        self.i += 2;
        Ok(v)
    }
    #[inline]
    pub fn varint(&mut self) -> Result<u64, String> {
        let mut v: u64 = 0;
        let mut shift = 0;
        loop {
            self.need(1)?;
            let b = self.d[self.i];
            self.i += 1;
            v |= ((b & 0x7F) as u64) << shift;
            if b & 0x80 == 0 {
                return Ok(v);
            }
            shift += 7;
            if shift > 63 {
                return Err("varint 溢出".into());
            }
        }
    }
    pub fn str(&mut self) -> Result<String, String> {
        let n = self.varint()? as usize;
        self.need(n)?;
        let s = std::str::from_utf8(&self.d[self.i..self.i + n])
            .map_err(|_| "非法 UTF-8")?
            .to_string();
        self.i += n;
        Ok(s)
    }
    pub fn bytes(&mut self) -> Result<Vec<u8>, String> {
        let n = self.varint()? as usize;
        self.need(n)?;
        let v = self.d[self.i..self.i + n].to_vec();
        self.i += n;
        Ok(v)
    }
    fn headers(&mut self) -> Result<HashMap<String, String>, String> {
        let n = self.u16()?;
        let mut h = HashMap::with_capacity(n as usize);
        for _ in 0..n {
            let k = self.str()?;
            let v = self.str()?;
            h.insert(k, v);
        }
        Ok(h)
    }
}

// ---------------- S -> C 编码 ----------------

pub fn encode_server(msg: &ServerMsg) -> Vec<u8> {
    let mut w = W::new();
    match msg {
        ServerMsg::RegisterAck { ok, error, tunnels } => {
            w.u8(T_REGISTER_ACK);
            w.u8(if *ok { 1 } else { 0 });
            w.str(error.as_deref().unwrap_or(""));
            w.u16(tunnels.len() as u16);
            for t in tunnels {
                w.str(&t.tunnel_id);
                w.u8(proto_to_u8(&t.proto));
                w.str(&t.public_url);
                w.u16(t.public_port.unwrap_or(0));
            }
        }
        ServerMsg::Ping => w.u8(T_PING),
        ServerMsg::OpenStream {
            stream_id,
            tunnel_id,
            proto,
            method,
            path,
            headers,
            body,
        } => {
            w.u8(T_OPEN_STREAM);
            w.varint(*stream_id);
            w.str(tunnel_id);
            w.u8(proto_to_u8(proto));
            w.str(method);
            w.str(path);
            w.headers(headers);
            w.bytes(body);
        }
        ServerMsg::CloseStream { stream_id } => {
            w.u8(T_CLOSE_STREAM);
            w.varint(*stream_id);
        }
        ServerMsg::Chunk { stream_id, data } => {
            w.u8(T_CHUNK);
            w.varint(*stream_id);
            w.bytes(data);
        }
    }
    w.buf
}

// ---------------- C -> S 解码 ----------------

pub fn decode_client(d: &[u8]) -> Result<ClientMsg, String> {
    let mut r = R::new(d);
    let t = r.u8()?;
    match t {
        T_REGISTER => {
            let client_id = r.str()?;
            let n = r.u16()?;
            let mut tunnels = Vec::with_capacity(n as usize);
            for _ in 0..n {
                let tunnel_id = r.str()?;
                let proto = u8_to_proto(r.u8()?);
                let sub = r.str()?;
                let path = r.str()?;
                let local = r.str()?;
                tunnels.push(TunnelDef {
                    tunnel_id,
                    proto: Some(proto),
                    subdomain: if sub.is_empty() { None } else { Some(sub) },
                    path_prefix: if path.is_empty() { None } else { Some(path) },
                    local_addr: if local.is_empty() { None } else { Some(local) },
                });
            }
            Ok(ClientMsg::Register {
                client_id: if client_id.is_empty() {
                    None
                } else {
                    Some(client_id)
                },
                tunnels,
            })
        }
        T_PONG => Ok(ClientMsg::Pong),
        T_RESPONSE_HEAD => {
            let stream_id = r.varint()?;
            let status = r.u16()?;
            let headers = r.headers()?;
            Ok(ClientMsg::ResponseHead {
                stream_id,
                status,
                headers,
            })
        }
        T_CHUNK => {
            let stream_id = r.varint()?;
            let data = r.bytes()?;
            Ok(ClientMsg::Chunk { stream_id, data })
        }
        T_END => Ok(ClientMsg::End {
            stream_id: r.varint()?,
        }),
        T_ABORT => {
            let stream_id = r.varint()?;
            let reason = r.str()?;
            Ok(ClientMsg::Abort { stream_id, reason })
        }
        other => Err(format!("未知帧类型 0x{other:02x}")),
    }
}

/// 仅用于测试：C -> S 编码
#[allow(dead_code)]
pub fn encode_client(msg: &ClientMsg) -> Vec<u8> {
    let mut w = W::new();
    match msg {
        ClientMsg::Register { client_id, tunnels } => {
            w.u8(T_REGISTER);
            w.str(client_id.as_deref().unwrap_or(""));
            w.u16(tunnels.len() as u16);
            for t in tunnels {
                w.str(&t.tunnel_id);
                w.u8(proto_to_u8(t.proto.as_deref().unwrap_or("http")));
                w.str(t.subdomain.as_deref().unwrap_or(""));
                w.str(t.path_prefix.as_deref().unwrap_or(""));
                w.str(t.local_addr.as_deref().unwrap_or(""));
            }
        }
        ClientMsg::Pong => w.u8(T_PONG),
        ClientMsg::ResponseHead {
            stream_id,
            status,
            headers,
        } => {
            w.u8(T_RESPONSE_HEAD);
            w.varint(*stream_id);
            w.u16(*status);
            w.headers(headers);
        }
        ClientMsg::Chunk { stream_id, data } => {
            w.u8(T_CHUNK);
            w.varint(*stream_id);
            w.bytes(data);
        }
        ClientMsg::End { stream_id } => {
            w.u8(T_END);
            w.varint(*stream_id);
        }
        ClientMsg::Abort { stream_id, reason } => {
            w.u8(T_ABORT);
            w.varint(*stream_id);
            w.str(reason);
        }
    }
    w.buf
}
