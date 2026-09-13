use crate::auth::{self, AppState, CurrentUser};
use crate::models::{TunnelRow, VisitorAuth};
use crate::util;
use axum::extract::{Path, State};
use axum::http::StatusCode;
use axum::response::{IntoResponse, Response};
use axum::Json;
use serde::Deserialize;
use serde_json::json;

type R = Response;

fn err(code: StatusCode, msg: &str) -> R {
    (code, Json(json!({ "error": msg }))).into_response()
}

pub fn tunnel_public_url(app: &AppState, t: &TunnelRow) -> String {
    match t.proto.as_str() {
        "tcp" => t.public_host.clone(),
        _ => {
            if let Some(p) = &t.path_prefix {
                format!("https://{}{}", app.cfg.base_domain, p)
            } else if !t.public_host.is_empty() {
                format!("https://{}.{}", t.public_host, app.cfg.base_domain)
            } else {
                format!("https://{}", app.cfg.base_domain)
            }
        }
    }
}

fn tunnel_json(app: &AppState, t: &TunnelRow, online: bool) -> serde_json::Value {
    json!({
        "tunnel_id": t.tunnel_id,
        "proto": t.proto,
        "public_host": t.public_host,
        "path_prefix": t.path_prefix,
        "local_addr": t.local_addr,
        "visitor_auth": t.visitor_auth,
        "disabled": t.disabled,
        "online": online,
        "public_url": tunnel_public_url(app, t),
        "created_at": t.created_at,
        "updated_at": t.updated_at,
    })
}

/// GET /api/tunnels
pub async fn list_tunnels(State(state): State<AppState>, CurrentUser(u): CurrentUser) -> R {
    let rows = sqlx::query_as::<_, TunnelRow>(
        "SELECT * FROM tunnels WHERE user_id = ? ORDER BY created_at",
    )
    .bind(u.id)
    .fetch_all(&state.db)
    .await
    .unwrap_or_default();
    let online = state.registry.online_users().await.contains(&u.id);
    let items: Vec<_> = rows.iter().map(|t| tunnel_json(&state, t, online)).collect();
    Json(json!({ "tunnels": items })).into_response()
}

#[derive(Deserialize)]
pub struct TunnelPatch {
    #[serde(default)]
    pub disabled: Option<bool>,
    #[serde(default)]
    pub visitor_auth: Option<VisitorAuth>,
}

/// PATCH /api/tunnels/:tunnel_id —— 改启用状态 / 访客鉴权（重连后生效）
pub async fn patch_tunnel(
    State(state): State<AppState>,
    CurrentUser(u): CurrentUser,
    Path(tunnel_id): Path<String>,
    Json(body): Json<TunnelPatch>,
) -> R {
    let row: Option<TunnelRow> =
        sqlx::query_as("SELECT * FROM tunnels WHERE user_id = ? AND tunnel_id = ?")
            .bind(u.id)
            .bind(&tunnel_id)
            .fetch_optional(&state.db)
            .await
            .unwrap_or(None);
    let Some(row) = row else {
        return err(StatusCode::NOT_FOUND, "隧道不存在");
    };

    if let Some(d) = body.disabled {
        let _ = sqlx::query("UPDATE tunnels SET disabled = ?, updated_at = ? WHERE id = ?")
            .bind(d)
            .bind(util::now())
            .bind(row.id)
            .execute(&state.db)
            .await;
    }
    if let Some(va) = &body.visitor_auth {
        let s = serde_json::to_string(va).unwrap_or_else(|_| "{}".into());
        let _ = sqlx::query("UPDATE tunnels SET visitor_auth = ?, updated_at = ? WHERE id = ?")
            .bind(&s)
            .bind(util::now())
            .bind(row.id)
            .execute(&state.db)
            .await;
    }
    crate::db::audit(
        &state.db,
        Some(&u.email),
        "tunnel.update",
        Some(&tunnel_id),
        Some(&format!(
            "disabled={:?} visitor_auth={}",
            body.disabled,
            body.visitor_auth.is_some()
        )),
        None,
    )
    .await;
    Json(json!({ "ok": true, "note": "重连客户端后生效" })).into_response()
}

/// DELETE /api/tunnels/:tunnel_id
pub async fn delete_tunnel(
    State(state): State<AppState>,
    CurrentUser(u): CurrentUser,
    Path(tunnel_id): Path<String>,
) -> R {
    let res = sqlx::query("DELETE FROM tunnels WHERE user_id = ? AND tunnel_id = ?")
        .bind(u.id)
        .bind(&tunnel_id)
        .execute(&state.db)
        .await;
    match res {
        Ok(r) if r.rows_affected() > 0 => {
            crate::db::audit(&state.db, Some(&u.email), "tunnel.delete", Some(&tunnel_id), None, None).await;
            Json(json!({ "ok": true })).into_response()
        }
        _ => err(StatusCode::NOT_FOUND, "隧道不存在"),
    }
}

/// GET /api/tokens
pub async fn list_tokens(State(state): State<AppState>, CurrentUser(u): CurrentUser) -> R {
    let rows = sqlx::query_as::<_, crate::models::ApiToken>(
        "SELECT * FROM api_tokens WHERE user_id = ? AND revoked = 0 ORDER BY created_at DESC",
    )
    .bind(u.id)
    .fetch_all(&state.db)
    .await
    .unwrap_or_default();
    Json(json!({ "tokens": rows })).into_response()
}

#[derive(Deserialize)]
pub struct CreateToken {
    #[serde(default = "default_token_name")]
    name: String,
}
fn default_token_name() -> String {
    "default".into()
}

/// POST /api/tokens —— 明文只在这一次返回
pub async fn create_token(
    State(state): State<AppState>,
    CurrentUser(u): CurrentUser,
    Json(body): Json<CreateToken>,
) -> R {
    match auth::create_api_token(&state.db, u.id, &body.name).await {
        Ok((raw, prefix)) => {
            crate::db::audit(&state.db, Some(&u.email), "token.create", Some(&prefix), Some(&body.name), None).await;
            Json(json!({ "token": raw, "prefix": prefix, "name": body.name })).into_response()
        }
        Err(e) => err(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()),
    }
}

/// DELETE /api/tokens/:id
pub async fn revoke_token(
    State(state): State<AppState>,
    CurrentUser(u): CurrentUser,
    Path(id): Path<i64>,
) -> R {
    let res = sqlx::query("UPDATE api_tokens SET revoked = 1 WHERE id = ? AND user_id = ?")
        .bind(id)
        .bind(u.id)
        .execute(&state.db)
        .await;
    match res {
        Ok(r) if r.rows_affected() > 0 => {
            crate::db::audit(&state.db, Some(&u.email), "token.revoke", Some(&id.to_string()), None, None).await;
            Json(json!({ "ok": true })).into_response()
        }
        _ => err(StatusCode::NOT_FOUND, "令牌不存在"),
    }
}

/// GET /api/usage
pub async fn usage(State(state): State<AppState>, CurrentUser(u): CurrentUser) -> R {
    let day = util::today();
    let row: Option<crate::models::UsageRow> =
        sqlx::query_as("SELECT day, bytes, requests FROM usage_daily WHERE user_id = ? AND day = ?")
            .bind(u.id)
            .bind(&day)
            .fetch_optional(&state.db)
            .await
            .unwrap_or(None);
    let (bytes, requests) = row.map(|r| (r.bytes, r.requests)).unwrap_or((0, 0));
    Json(json!({
        "day": day,
        "bytes": bytes,
        "requests": requests,
        "daily_bytes_limit": u.daily_bytes,
    }))
    .into_response()
}

pub fn routes() -> axum::Router<AppState> {
    use axum::routing::{delete, get, patch};
    axum::Router::new()
        .route("/api/tunnels", get(list_tunnels))
        .route("/api/tunnels/:tunnel_id", patch(patch_tunnel).delete(delete_tunnel))
        .route("/api/tokens", get(list_tokens).post(create_token))
        .route("/api/tokens/:id", delete(revoke_token))
        .route("/api/usage", get(usage))
}
