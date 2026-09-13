use base64::Engine;
use rand::RngCore;
use sha2::{Digest, Sha256};

pub fn now() -> String {
    chrono::Utc::now().to_rfc3339()
}

pub fn today() -> String {
    chrono::Utc::now().format("%Y-%m-%d").to_string()
}

pub fn sha256_hex(s: &str) -> String {
    let mut h = Sha256::new();
    h.update(s.as_bytes());
    hex::encode(h.finalize())
}

pub fn b64url(bytes: &[u8]) -> String {
    base64::engine::general_purpose::URL_SAFE_NO_PAD.encode(bytes)
}

pub fn random_bytes(n: usize) -> Vec<u8> {
    let mut b = vec![0u8; n];
    rand::thread_rng().fill_bytes(&mut b);
    b
}

/// 会话 ID / PKCE verifier 等
pub fn random_id(n: usize) -> String {
    b64url(&random_bytes(n))
}

/// 给客户端的明文 API Token：tun_<random>
pub fn new_api_token() -> (String, String, String) {
    let raw = format!("tun_{}", b64url(&random_bytes(32)));
    let hash = sha256_hex(&raw);
    let prefix = raw.chars().take(12).collect::<String>();
    (raw, hash, prefix)
}

/// email/name -> 命名空间 slug（小写字母数字连字符）
pub fn slugify(input: &str) -> String {
    let base = input.split('@').next().unwrap_or(input);
    let mut out = String::new();
    let mut last_dash = false;
    for c in base.chars() {
        let c = c.to_ascii_lowercase();
        if c.is_ascii_alphanumeric() {
            out.push(c);
            last_dash = false;
        } else if !last_dash {
            out.push('-');
            last_dash = true;
        }
    }
    let out = out.trim_matches('-').to_string();
    if out.is_empty() {
        "user".to_string()
    } else {
        out.chars().take(32).collect()
    }
}

pub fn get_cookie(headers: &axum::http::HeaderMap, name: &str) -> Option<String> {
    let raw = headers.get(axum::http::header::COOKIE)?.to_str().ok()?;
    for part in raw.split(';') {
        let part = part.trim();
        if let Some(v) = part.strip_prefix(&format!("{name}=")) {
            return Some(v.to_string());
        }
    }
    None
}

/// 解析 Authorization: Basic base64(user:pass)
pub fn parse_basic_auth(value: &str) -> Option<(String, String)> {
    let b64 = value.strip_prefix("Basic ")?;
    let decoded = base64::engine::general_purpose::STANDARD.decode(b64).ok()?;
    let s = String::from_utf8(decoded).ok()?;
    let (u, p) = s.split_once(':')?;
    Some((u.to_string(), p.to_string()))
}

/// 取出 Authorization: Bearer xxx
pub fn parse_bearer(headers: &axum::http::HeaderMap) -> Option<String> {
    let raw = headers.get(axum::http::header::AUTHORIZATION)?.to_str().ok()?;
    let t = raw.strip_prefix("Bearer ")?;
    Some(t.to_string())
}
