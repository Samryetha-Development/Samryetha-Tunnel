use crate::auth::{self, AppState, CurrentUser};
use crate::config::AuthMode;
use crate::util;
use axum::extract::{Query, State};
use axum::http::{header, StatusCode};
use axum::response::{IntoResponse, Redirect, Response};
use axum::Json;
use serde::Deserialize;
use serde_json::json;
use std::collections::HashMap;

type R = Response;

fn error(code: StatusCode, msg: &str) -> R {
    (code, Json(json!({ "error": msg }))).into_response()
}

#[derive(Deserialize)]
pub struct CallbackQuery {
    code: Option<String>,
    state: Option<String>,
    error: Option<String>,
    error_description: Option<String>,
}

/// GET /auth/login —— 浏览器登录入口
pub async fn login(State(state): State<AppState>) -> R {
    match state.cfg.auth_mode {
        AuthMode::Dev => dev_login_page(),
        AuthMode::Oidc => {
            let Some(oidc) = state.cfg.oidc.as_ref() else {
                return error(StatusCode::INTERNAL_SERVER_ERROR, "OIDC 未配置");
            };
            let Some(disc) = state.oidc.discovery.as_ref() else {
                return error(StatusCode::SERVICE_UNAVAILABLE, "IdP discovery 不可用");
            };
            let verifier = util::random_id(32);
            let challenge = util::b64url(&sha256(verifier.as_bytes()));
            let csrf = util::random_id(24);
            {
                let mut states = state.oidc.states.lock().await;
                states.retain(|_, s| s.created.elapsed().as_secs() < 600);
                states.insert(
                    csrf.clone(),
                    auth::LoginState {
                        verifier,
                        created: std::time::Instant::now(),
                    },
                );
            }
            let url = format!(
                "{}?response_type=code&client_id={}&redirect_uri={}&scope={}&state={}&code_challenge={}&code_challenge_method=S256",
                disc.authorization_endpoint,
                urlencoding(&oidc.client_id),
                urlencoding(&oidc.redirect_uri),
                urlencoding(&oidc.scopes),
                urlencoding(&csrf),
                urlencoding(&challenge),
            );
            Redirect::temporary(&url).into_response()
        }
    }
}

/// GET /auth/callback
pub async fn callback(State(state): State<AppState>, Query(q): Query<CallbackQuery>) -> R {
    if let Some(e) = q.error {
        return error(
            StatusCode::UNAUTHORIZED,
            &format!("登录被拒绝: {e} {}", q.error_description.unwrap_or_default()),
        );
    }
    let (Some(code), Some(csrf)) = (q.code, q.state) else {
        return error(StatusCode::BAD_REQUEST, "缺少 code 或 state");
    };
    let verifier = {
        let mut states = state.oidc.states.lock().await;
        states.remove(&csrf).map(|s| s.verifier)
    };
    let Some(verifier) = verifier else {
        return error(StatusCode::BAD_REQUEST, "state 无效或已过期");
    };

    let tr = match auth::exchange_code(&state, &code, &verifier).await {
        Ok(t) => t,
        Err(e) => return error(StatusCode::UNAUTHORIZED, &e),
    };
    let Some(id_token) = tr.id_token else {
        return error(StatusCode::UNAUTHORIZED, "IdP 未返回 id_token");
    };
    let claims = match auth::verify_id_token(&state, &id_token).await {
        Ok(c) => c,
        Err(e) => return error(StatusCode::UNAUTHORIZED, &e),
    };
    let user = match auth::upsert_from_claims(&state, claims.email.as_deref(), claims.name.as_deref()).await {
        Ok(u) => u,
        Err(e) => return error(StatusCode::UNAUTHORIZED, &e),
    };
    let sid = match auth::create_session(&state.db, user.id).await {
        Ok(s) => s,
        Err(e) => return error(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()),
    };
    crate::db::audit(
        &state.db,
        Some(&user.email),
        "auth.login",
        Some("web"),
        Some(&format!("sub={}", claims.sub)),
        None,
    )
    .await;
    let mut resp = Redirect::to("/").into_response();
    resp.headers_mut().insert(
        header::SET_COOKIE,
        state.session_cookie(&sid, 7 * 86400).parse().unwrap(),
    );
    resp
}

/// GET /auth/logout
pub async fn logout(State(state): State<AppState>, headers: axum::http::HeaderMap) -> R {
    if let Some(sid) = util::get_cookie(&headers, "sid") {
        let _ = sqlx::query("DELETE FROM sessions WHERE id = ?")
            .bind(&sid)
            .execute(&state.db)
            .await;
    }
    let mut resp = Redirect::to("/").into_response();
    resp.headers_mut()
        .insert(header::SET_COOKIE, state.clear_cookie().parse().unwrap());
    resp
}

/// GET /auth/me
pub async fn me(CurrentUser(u): CurrentUser) -> R {
    Json(json!({
        "id": u.id,
        "email": u.email,
        "name": u.name,
        "slug": u.slug,
        "role": u.role,
        "max_tunnels": u.max_tunnels,
        "daily_bytes": u.daily_bytes,
    }))
    .into_response()
}

/// GET /auth/config —— 客户端用来判断该走哪种登录、域名等
pub async fn config(State(state): State<AppState>) -> R {
    Json(json!({
        "auth_mode": auth::auth_mode_name(&state.cfg),
        "base_domain": state.cfg.base_domain,
        "device_enabled": state.cfg.oidc.as_ref().map(|o| o.device_enabled).unwrap_or(false),
        "role": match state.cfg.role {
            crate::config::Role::Server => "server",
            crate::config::Role::Relay => "relay",
        },
    }))
    .into_response()
}

/// POST /auth/dev-login（仅 AUTH_MODE=dev）
#[derive(Deserialize)]
pub struct DevLogin {
    email: String,
    #[serde(default)]
    name: Option<String>,
}

pub async fn dev_login(State(state): State<AppState>, Json(body): Json<DevLogin>) -> R {
    if state.cfg.auth_mode != AuthMode::Dev {
        return error(StatusCode::NOT_FOUND, "dev 登录已禁用");
    }
    let is_admin = state
        .cfg
        .admin_emails
        .iter()
        .any(|a| a == &body.email.to_lowercase());
    let user = match crate::db::upsert_user(
        &state.db,
        &body.email,
        body.name.as_deref(),
        is_admin,
        state.cfg.default_max_tunnels,
        state.cfg.default_daily_bytes,
    )
    .await
    {
        Ok(u) => u,
        Err(e) => return error(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()),
    };
    let sid = match auth::create_session(&state.db, user.id).await {
        Ok(s) => s,
        Err(e) => return error(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()),
    };
    let mut resp = Json(json!({
        "ok": true,
        "user": {"id": user.id, "email": user.email, "slug": user.slug, "role": user.role}
    }))
    .into_response();
    resp.headers_mut().insert(
        header::SET_COOKIE,
        state.session_cookie(&sid, 7 * 86400).parse().unwrap(),
    );
    resp
}

/// POST /auth/dev-token（仅 dev，给桌面客户端联调用）
pub async fn dev_token(State(state): State<AppState>, Json(body): Json<DevLogin>) -> R {
    if state.cfg.auth_mode != AuthMode::Dev {
        return error(StatusCode::NOT_FOUND, "dev 登录已禁用");
    }
    let is_admin = state
        .cfg
        .admin_emails
        .iter()
        .any(|a| a == &body.email.to_lowercase());
    let user = match crate::db::upsert_user(
        &state.db,
        &body.email,
        body.name.as_deref(),
        is_admin,
        state.cfg.default_max_tunnels,
        state.cfg.default_daily_bytes,
    )
    .await
    {
        Ok(u) => u,
        Err(e) => return error(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()),
    };
    match auth::create_api_token(&state.db, user.id, "dev").await {
        Ok((raw, prefix)) => Json(json!({
            "token": raw,
            "prefix": prefix,
            "user": {"id": user.id, "email": user.email, "slug": user.slug, "role": user.role}
        }))
        .into_response(),
        Err(e) => error(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()),
    }
}

/// POST /auth/device/start
pub async fn device_start(State(state): State<AppState>) -> R {
    match auth::device_start(&state).await {
        Ok(d) => Json(json!({
            "device_code": d.device_code,
            "user_code": d.user_code,
            "verification_uri": d.verification_uri,
            "verification_uri_complete": d.verification_uri_complete,
            "expires_in": d.expires_in,
            "interval": if d.interval > 0 { d.interval } else { 5 },
        }))
        .into_response(),
        Err(e) => error(StatusCode::BAD_REQUEST, &e),
    }
}

#[derive(Deserialize)]
pub struct DevicePoll {
    device_code: String,
}

/// POST /auth/device/poll —— 成功后签发本系统 API Token
pub async fn device_poll(State(state): State<AppState>, Json(body): Json<DevicePoll>) -> R {
    let tr = match auth::device_poll(&state, &body.device_code).await {
        Ok(t) => t,
        Err(e) => return error(StatusCode::BAD_REQUEST, &e),
    };
    if let Some(err) = &tr.error {
        let pending = matches!(err.as_str(), "authorization_pending" | "slow_down");
        return Json(json!({
            "status": if pending { "pending" } else { "error" },
            "error": err,
            "description": tr.error_description,
        }))
        .into_response();
    }
    let Some(id_token) = tr.id_token else {
        return error(StatusCode::UNAUTHORIZED, "IdP 未返回 id_token");
    };
    let claims = match auth::verify_id_token(&state, &id_token).await {
        Ok(c) => c,
        Err(e) => return error(StatusCode::UNAUTHORIZED, &e),
    };
    let user = match auth::upsert_from_claims(&state, claims.email.as_deref(), claims.name.as_deref()).await {
        Ok(u) => u,
        Err(e) => return error(StatusCode::UNAUTHORIZED, &e),
    };
    let (raw, prefix) = match auth::create_api_token(&state.db, user.id, "device").await {
        Ok(v) => v,
        Err(e) => return error(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()),
    };
    crate::db::audit(&state.db, Some(&user.email), "auth.device_login", Some(&prefix), None, None).await;
    Json(json!({
        "status": "ok",
        "token": raw,
        "prefix": prefix,
        "user": {"id": user.id, "email": user.email, "slug": user.slug, "role": user.role}
    }))
    .into_response()
}

pub fn routes() -> axum::Router<AppState> {
    use axum::routing::{get, post};
    axum::Router::new()
        .route("/auth/login", get(login))
        .route("/auth/callback", get(callback))
        .route("/auth/logout", get(logout))
        .route("/auth/me", get(me))
        .route("/auth/config", get(config))
        .route("/auth/dev-login", post(dev_login))
        .route("/auth/dev-token", post(dev_token))
        .route("/auth/device/start", post(device_start))
        .route("/auth/device/poll", post(device_poll))
}

// ---------- helpers ----------

fn sha256(b: &[u8]) -> Vec<u8> {
    use sha2::{Digest, Sha256};
    let mut h = Sha256::new();
    h.update(b);
    h.finalize().to_vec()
}

fn urlencoding(s: &str) -> String {
    let mut out = String::new();
    for b in s.bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => out.push(b as char),
            _ => out.push_str(&format!("%{b:02X}")),
        }
    }
    out
}

fn dev_login_page() -> R {
    let html = r#"<!doctype html><html lang="zh"><head><meta charset="utf-8">
<title>Tunnel 登录（开发模式）</title>
<style>
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:#0b0e14;color:#e6e6e6;display:flex;align-items:center;justify-content:center;height:100vh;margin:0}
.card{background:#141922;border:1px solid #222b38;border-radius:14px;padding:28px 30px;width:340px;box-shadow:0 20px 60px rgba(0,0,0,.5)}
h1{font-size:16px;margin:0 0 4px}p{font-size:12px;color:#8b98a9;margin:0 0 18px}
input{width:100%;box-sizing:border-box;background:#0b0e14;border:1px solid #2a3442;color:#e6e6e6;border-radius:9px;padding:10px 12px;font-size:13px;outline:none}
input:focus{border-color:#3ba55d}
button{margin-top:12px;width:100%;background:#3ba55d;color:#04240f;border:0;border-radius:9px;padding:10px;font-size:13px;font-weight:600;cursor:pointer}
.err{margin-top:10px;font-size:12px;color:#ff6b6b}
</style></head><body>
<div class="card">
  <h1>Tunnel 开发登录</h1>
  <p>当前 AUTH_MODE=dev，仅本机测试用。输入邮箱即建立会话。</p>
  <input id="email" placeholder="you@example.com" autofocus>
  <button onclick="go()">登录</button>
  <div class="err" id="err"></div>
</div>
<script>
async function go(){
  const email=document.getElementById('email').value.trim();
  if(!email){document.getElementById('err').textContent='请输入邮箱';return}
  const r=await fetch('/auth/dev-login',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({email})});
  if(r.ok){location.href='/'}else{const j=await r.json().catch(()=>({}));document.getElementById('err').textContent=j.error||'登录失败'}
}
document.getElementById('email').addEventListener('keydown',e=>{if(e.key==='Enter')go()});
</script></body></html>"#;
    ([(header::CONTENT_TYPE, "text/html; charset=utf-8")], html).into_response()
}

/// 供其它模块复用：query 解析
pub fn _unused_q(_: &HashMap<String, String>) {}
