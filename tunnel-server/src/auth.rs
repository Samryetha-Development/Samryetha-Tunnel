use crate::config::{AuthMode, Config};
use crate::db::Db;
use crate::models::User;
use crate::util;
use axum::extract::FromRequestParts;
use axum::http::request::Parts;
use axum::http::StatusCode;
use serde::Deserialize;
use std::collections::HashMap;
use std::sync::Arc;
use std::time::Instant;
use tokio::sync::{Mutex, RwLock};

#[derive(Clone, Debug, Deserialize)]
#[allow(dead_code)]
pub struct Discovery {
    pub issuer: String,
    pub authorization_endpoint: String,
    pub token_endpoint: String,
    pub jwks_uri: String,
    #[serde(default)]
    pub device_authorization_endpoint: Option<String>,
    #[serde(default)]
    pub end_session_endpoint: Option<String>,
}

#[derive(Clone)]
pub struct LoginState {
    pub verifier: String,
    pub created: Instant,
}

pub struct OidcRuntime {
    pub discovery: Option<Discovery>,
    pub states: Mutex<HashMap<String, LoginState>>,
    pub jwks: RwLock<Option<(Vec<jsonwebtoken::jwk::Jwk>, Instant)>>,
}

/// 应用共享状态
#[derive(Clone)]
pub struct AppState {
    pub cfg: Config,
    pub db: Db,
    pub registry: crate::registry::Registry,
    pub http: reqwest::Client,
    pub oidc: Arc<OidcRuntime>,
    pub usage: crate::quota::UsageMeter,
    pub limiter: crate::quota::RateLimiter,
    pub streams: Arc<Mutex<HashMap<i64, i64>>>,
}

impl AppState {
    pub async fn stream_acquire(&self, user_id: i64) -> bool {
        let mut g = self.streams.lock().await;
        let c = g.entry(user_id).or_insert(0);
        if *c >= self.cfg.max_streams_per_user {
            return false;
        }
        *c += 1;
        true
    }

    pub async fn stream_release(&self, user_id: i64) {
        let mut g = self.streams.lock().await;
        if let Some(c) = g.get_mut(&user_id) {
            *c = (*c - 1).max(0);
        }
    }
}

impl AppState {
    pub fn session_cookie(&self, sid: &str, max_age_secs: i64) -> String {
        let secure = if self.cfg.cookie_secure { "; Secure" } else { "" };
        format!(
            "sid={sid}; Path=/; HttpOnly; SameSite=Lax; Max-Age={max_age_secs}{secure}"
        )
    }

    pub fn clear_cookie(&self) -> String {
        let secure = if self.cfg.cookie_secure { "; Secure" } else { "" };
        format!("sid=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0{secure}")
    }
}

// ===================== OIDC =====================

/// 启动时预取 discovery（失败不阻塞启动，登录时再重试）
pub async fn load_discovery(cfg: &Config, http: &reqwest::Client) -> Result<Discovery, String> {
    let oidc = cfg.oidc.as_ref().ok_or("非 OIDC 模式")?;
    let url = format!("{}/.well-known/openid-configuration", oidc.issuer);
    let resp = http
        .get(&url)
        .send()
        .await
        .map_err(|e| format!("discovery 请求失败: {e}"))?;
    if !resp.status().is_success() {
        return Err(format!("discovery 返回 {}", resp.status()));
    }
    let d: Discovery = resp.json().await.map_err(|e| format!("discovery 解析失败: {e}"))?;
    Ok(d)
}

#[derive(Debug, Deserialize)]
#[allow(dead_code)]
pub struct TokenResponse {
    pub id_token: Option<String>,
    pub access_token: Option<String>,
    #[serde(default)]
    pub error: Option<String>,
    #[serde(default)]
    pub error_description: Option<String>,
}

#[derive(Debug, Clone)]
pub struct IdClaims {
    pub sub: String,
    pub email: Option<String>,
    pub name: Option<String>,
}

pub async fn verify_id_token(
    state: &AppState,
    id_token: &str,
) -> Result<IdClaims, String> {
    let oidc = state.cfg.oidc.as_ref().ok_or("非 OIDC 模式")?;
    let disc = state
        .oidc
        .discovery
        .clone()
        .ok_or("discovery 未就绪")?;

    let keys = fetch_jwks(state, &disc.jwks_uri).await?;
    let mut validation = jsonwebtoken::Validation::new(jsonwebtoken::Algorithm::RS256);
    validation.algorithms = vec![
        jsonwebtoken::Algorithm::RS256,
        jsonwebtoken::Algorithm::RS384,
        jsonwebtoken::Algorithm::RS512,
        jsonwebtoken::Algorithm::ES256,
        jsonwebtoken::Algorithm::ES384,
    ];
    validation.set_issuer(&[disc.issuer.as_str()]);
    validation.set_audience(&[oidc.client_id.as_str()]);

    let mut last_err = String::from("无可用 JWK");
    for jwk in &keys {
        let key = match jsonwebtoken::DecodingKey::from_jwk(jwk) {
            Ok(k) => k,
            Err(e) => {
                last_err = e.to_string();
                continue;
            }
        };
        match jsonwebtoken::decode::<serde_json::Value>(id_token, &key, &validation) {
            Ok(data) => {
                let v = data.claims;
                let sub = v.get("sub").and_then(|x| x.as_str()).unwrap_or("").to_string();
                let email = v.get("email").and_then(|x| x.as_str()).map(|s| s.to_string());
                let name = v
                    .get("name")
                    .or_else(|| v.get("preferred_username"))
                    .and_then(|x| x.as_str())
                    .map(|s| s.to_string());
                if sub.is_empty() {
                    return Err("id_token 缺少 sub".into());
                }
                return Ok(IdClaims { sub, email, name });
            }
            Err(e) => last_err = e.to_string(),
        }
    }
    Err(format!("id_token 校验失败: {last_err}"))
}

async fn fetch_jwks(
    state: &AppState,
    jwks_uri: &str,
) -> Result<Vec<jsonwebtoken::jwk::Jwk>, String> {
    {
        let cached = state.oidc.jwks.read().await;
        if let Some((keys, at)) = cached.as_ref() {
            if at.elapsed().as_secs() < 3600 {
                return Ok(keys.clone());
            }
        }
    }
    let resp = state
        .http
        .get(jwks_uri)
        .send()
        .await
        .map_err(|e| format!("JWKS 请求失败: {e}"))?;
    let set: jsonwebtoken::jwk::JwkSet = resp.json().await.map_err(|e| format!("JWKS 解析失败: {e}"))?;
    let keys = set.keys;
    let mut cached = state.oidc.jwks.write().await;
    *cached = Some((keys.clone(), Instant::now()));
    Ok(keys)
}

pub async fn exchange_code(
    state: &AppState,
    code: &str,
    verifier: &str,
) -> Result<TokenResponse, String> {
    let oidc = state.cfg.oidc.as_ref().ok_or("非 OIDC 模式")?;
    let disc = state.oidc.discovery.clone().ok_or("discovery 未就绪")?;
    let params = [
        ("grant_type", "authorization_code"),
        ("code", code),
        ("redirect_uri", &oidc.redirect_uri),
        ("client_id", &oidc.client_id),
        ("client_secret", &oidc.client_secret),
        ("code_verifier", verifier),
    ];
    let resp = state
        .http
        .post(&disc.token_endpoint)
        .form(&params)
        .send()
        .await
        .map_err(|e| format!("换取 token 失败: {e}"))?;
    let tr: TokenResponse = resp.json().await.map_err(|e| format!("token 响应解析失败: {e}"))?;
    if let Some(err) = &tr.error {
        return Err(format!(
            "IdP 返回错误 {}: {}",
            err,
            tr.error_description.clone().unwrap_or_default()
        ));
    }
    Ok(tr)
}

#[derive(Debug, Deserialize)]
pub struct DeviceAuthResponse {
    pub device_code: String,
    pub user_code: String,
    pub verification_uri: String,
    #[serde(default)]
    pub verification_uri_complete: Option<String>,
    pub expires_in: i64,
    #[serde(default)]
    pub interval: i64,
}

pub async fn device_start(state: &AppState) -> Result<DeviceAuthResponse, String> {
    let oidc = state.cfg.oidc.as_ref().ok_or("非 OIDC 模式")?;
    let disc = state.oidc.discovery.clone().ok_or("discovery 未就绪")?;
    let endpoint = disc
        .device_authorization_endpoint
        .clone()
        .ok_or("IdP 未声明 device_authorization_endpoint（不支持设备码）")?;
    let params = [("client_id", oidc.client_id.as_str()), ("scope", oidc.scopes.as_str())];
    let resp = state
        .http
        .post(&endpoint)
        .form(&params)
        .send()
        .await
        .map_err(|e| format!("device 请求失败: {e}"))?;
    let body: serde_json::Value = resp.json().await.map_err(|e| format!("device 响应解析失败: {e}"))?;
    serde_json::from_value(body).map_err(|e| format!("device 响应字段缺失: {e}"))
}

pub async fn device_poll(
    state: &AppState,
    device_code: &str,
) -> Result<TokenResponse, String> {
    let oidc = state.cfg.oidc.as_ref().ok_or("非 OIDC 模式")?;
    let disc = state.oidc.discovery.clone().ok_or("discovery 未就绪")?;
    let params = [
        ("grant_type", "urn:ietf:params:oauth:grant-type:device_code"),
        ("device_code", device_code),
        ("client_id", &oidc.client_id),
        ("client_secret", &oidc.client_secret),
    ];
    let resp = state
        .http
        .post(&disc.token_endpoint)
        .form(&params)
        .send()
        .await
        .map_err(|e| format!("device 轮询失败: {e}"))?;
    let tr: TokenResponse = resp.json().await.map_err(|e| format!("device token 解析失败: {e}"))?;
    Ok(tr)
}

// ===================== 会话 / Token =====================

pub async fn create_session(db: &Db, user_id: i64) -> Result<String, sqlx::Error> {
    let sid = util::random_id(32);
    let now = chrono::Utc::now();
    let expires = now + chrono::Duration::days(7);
    sqlx::query("INSERT INTO sessions (id, user_id, created_at, expires_at) VALUES (?, ?, ?, ?)")
        .bind(&sid)
        .bind(user_id)
        .bind(now.to_rfc3339())
        .bind(expires.to_rfc3339())
        .execute(db)
        .await?;
    Ok(sid)
}

pub async fn session_user(db: &Db, sid: &str) -> Option<User> {
    let now = util::now();
    sqlx::query_as::<_, User>(
        "SELECT u.* FROM users u JOIN sessions s ON s.user_id = u.id \
         WHERE s.id = ? AND s.expires_at > ? AND u.disabled = 0",
    )
    .bind(sid)
    .bind(&now)
    .fetch_optional(db)
    .await
    .ok()
    .flatten()
}

pub async fn api_token_user(db: &Db, raw: &str) -> Option<User> {
    let hash = util::sha256_hex(raw);
    let user: Option<User> = sqlx::query_as::<_, User>(
        "SELECT u.* FROM users u JOIN api_tokens t ON t.user_id = u.id \
         WHERE t.token_hash = ? AND t.revoked = 0 AND u.disabled = 0",
    )
    .bind(&hash)
    .fetch_optional(db)
    .await
    .ok()
    .flatten();
    if let Some(u) = &user {
        let _ = sqlx::query("UPDATE api_tokens SET last_used_at = ? WHERE token_hash = ?")
            .bind(util::now())
            .bind(&hash)
            .execute(db)
            .await;
        let _ = u;
    }
    user
}

pub async fn create_api_token(db: &Db, user_id: i64, name: &str) -> Result<(String, String), sqlx::Error> {
    let (raw, hash, prefix) = util::new_api_token();
    sqlx::query("INSERT INTO api_tokens (user_id, name, token_hash, prefix, created_at) VALUES (?, ?, ?, ?, ?)")
        .bind(user_id)
        .bind(name)
        .bind(&hash)
        .bind(&prefix)
        .bind(util::now())
        .execute(db)
        .await?;
    Ok((raw, prefix))
}

/// 登录后从 claims 落地用户
pub async fn upsert_from_claims(
    state: &AppState,
    email: Option<&str>,
    name: Option<&str>,
) -> Result<User, String> {
    let email = email.ok_or("IdP 未返回 email，无法建立账户")?;
    let is_admin = state
        .cfg
        .admin_emails
        .iter()
        .any(|a| a == &email.to_lowercase());
    crate::db::upsert_user(
        &state.db,
        email,
        name,
        is_admin,
        state.cfg.default_max_tunnels,
        state.cfg.default_daily_bytes,
    )
    .await
    .map_err(|e| e.to_string())
}

pub fn auth_mode_name(cfg: &Config) -> &'static str {
    match cfg.auth_mode {
        AuthMode::Oidc => "oidc",
        AuthMode::Dev => "dev",
    }
}

// ===================== 提取器 =====================

pub struct CurrentUser(pub User);

#[axum::async_trait]
impl FromRequestParts<AppState> for CurrentUser {
    type Rejection = (StatusCode, String);

    async fn from_request_parts(
        parts: &mut Parts,
        state: &AppState,
    ) -> Result<Self, Self::Rejection> {
        if let Some(raw) = util::parse_bearer(&parts.headers) {
            if let Some(u) = api_token_user(&state.db, &raw).await {
                return Ok(CurrentUser(u));
            }
            return Err((StatusCode::UNAUTHORIZED, "无效的 API Token".into()));
        }
        if let Some(sid) = util::get_cookie(&parts.headers, "sid") {
            if let Some(u) = session_user(&state.db, &sid).await {
                return Ok(CurrentUser(u));
            }
        }
        Err((StatusCode::UNAUTHORIZED, "未登录".into()))
    }
}

pub struct AdminUser(pub User);

#[axum::async_trait]
impl FromRequestParts<AppState> for AdminUser {
    type Rejection = (StatusCode, String);

    async fn from_request_parts(
        parts: &mut Parts,
        state: &AppState,
    ) -> Result<Self, Self::Rejection> {
        let CurrentUser(u) = CurrentUser::from_request_parts(parts, state).await?;
        if !u.is_admin() {
            return Err((StatusCode::FORBIDDEN, "需要管理员权限".into()));
        }
        Ok(AdminUser(u))
    }
}
