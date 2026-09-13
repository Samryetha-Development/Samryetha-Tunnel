use crate::auth::{AdminUser, AppState};
use crate::models::{AuditRow, TunnelRow, User};
use crate::util;
use axum::extract::{Path, Query, State};
use axum::http::StatusCode;
use axum::response::{IntoResponse, Response};
use axum::Json;
use serde::Deserialize;
use serde_json::json;

type R = Response;

fn err(code: StatusCode, msg: &str) -> R {
    (code, Json(json!({ "error": msg }))).into_response()
}

/// GET /api/admin/overview
pub async fn overview(State(state): State<AppState>, AdminUser(_admin): AdminUser) -> R {
    let users: i64 = sqlx::query_scalar("SELECT COUNT(*) FROM users")
        .fetch_one(&state.db)
        .await
        .unwrap_or(0);
    let tunnels: i64 = sqlx::query_scalar("SELECT COUNT(*) FROM tunnels")
        .fetch_one(&state.db)
        .await
        .unwrap_or(0);
    let online = state.registry.online_users().await.len();
    let requests: i64 = sqlx::query_scalar("SELECT COALESCE(SUM(requests),0) FROM usage_daily")
        .fetch_one(&state.db)
        .await
        .unwrap_or(0);
    Json(json!({
        "users": users,
        "tunnels": tunnels,
        "online_clients": online,
        "requests_today": requests,
    }))
    .into_response()
}

/// GET /api/admin/users
pub async fn list_users(State(state): State<AppState>, AdminUser(_admin): AdminUser) -> R {
    let users = sqlx::query_as::<_, User>("SELECT * FROM users ORDER BY created_at")
        .fetch_all(&state.db)
        .await
        .unwrap_or_default();
    let online = state.registry.online_users().await;
    let items: Vec<_> = users
        .iter()
        .map(|u| {
            json!({
                "id": u.id, "email": u.email, "name": u.name, "slug": u.slug,
                "role": u.role, "max_tunnels": u.max_tunnels, "daily_bytes": u.daily_bytes,
                "disabled": u.disabled, "online": online.contains(&u.id),
                "created_at": u.created_at,
            })
        })
        .collect();
    Json(json!({ "users": items })).into_response()
}

#[derive(Deserialize)]
pub struct UserPatch {
    #[serde(default)]
    pub role: Option<String>,
    #[serde(default)]
    pub disabled: Option<bool>,
    #[serde(default)]
    pub max_tunnels: Option<i64>,
    #[serde(default)]
    pub daily_bytes: Option<i64>,
}

/// PATCH /api/admin/users/:id
pub async fn patch_user(
    State(state): State<AppState>,
    AdminUser(admin): AdminUser,
    Path(id): Path<i64>,
    Json(body): Json<UserPatch>,
) -> R {
    if let Some(role) = &body.role {
        if role != "admin" && role != "user" {
            return err(StatusCode::BAD_REQUEST, "role 只能是 admin/user");
        }
    }
    let target: Option<User> = sqlx::query_as("SELECT * FROM users WHERE id = ?")
        .bind(id)
        .fetch_optional(&state.db)
        .await
        .unwrap_or(None);
    if target.is_none() {
        return err(StatusCode::NOT_FOUND, "用户不存在");
    }
    if body.disabled == Some(true) && id == admin.id {
        return err(StatusCode::BAD_REQUEST, "不能停用自己");
    }

    let mut sets: Vec<&str> = Vec::new();
    if body.role.is_some() {
        sets.push("role = ?");
    }
    if body.disabled.is_some() {
        sets.push("disabled = ?");
    }
    if body.max_tunnels.is_some() {
        sets.push("max_tunnels = ?");
    }
    if body.daily_bytes.is_some() {
        sets.push("daily_bytes = ?");
    }
    if sets.is_empty() {
        return Json(json!({ "ok": true })).into_response();
    }
    let sql = format!("UPDATE users SET {} WHERE id = ?", sets.join(", "));
    let mut q = sqlx::query(&sql);
    if let Some(v) = &body.role {
        q = q.bind(v);
    }
    if let Some(v) = body.disabled {
        q = q.bind(v);
    }
    if let Some(v) = body.max_tunnels {
        q = q.bind(v);
    }
    if let Some(v) = body.daily_bytes {
        q = q.bind(v);
    }
    q = q.bind(id);
    let _ = q.execute(&state.db).await;

    if body.disabled == Some(true) {
        state.registry.remove_client(id).await;
    }
    crate::db::audit(
        &state.db,
        Some(&admin.email),
        "admin.user_update",
        Some(&id.to_string()),
        Some(&format!(
            "role={:?} disabled={:?} max_tunnels={:?} daily_bytes={:?}",
            body.role, body.disabled, body.max_tunnels, body.daily_bytes
        )),
        None,
    )
    .await;
    Json(json!({ "ok": true })).into_response()
}

/// GET /api/admin/tunnels
pub async fn list_tunnels(State(state): State<AppState>, AdminUser(_admin): AdminUser) -> R {
    let rows: Vec<TunnelRow> =
        sqlx::query_as("SELECT * FROM tunnels ORDER BY created_at DESC")
            .fetch_all(&state.db)
            .await
            .unwrap_or_default();
    let online = state.registry.online_users().await;
    let items: Vec<_> = rows
        .iter()
        .map(|t| {
            json!({
                "id": t.id, "user_id": t.user_id, "tunnel_id": t.tunnel_id, "proto": t.proto,
                "public_host": t.public_host, "path_prefix": t.path_prefix, "local_addr": t.local_addr,
                "disabled": t.disabled, "online": online.contains(&t.user_id),
                "public_url": crate::routes_api::tunnel_public_url(&state, t),
            })
        })
        .collect();
    Json(json!({ "tunnels": items })).into_response()
}

#[derive(Deserialize)]
pub struct TunnelPatch {
    pub disabled: bool,
}

/// PATCH /api/admin/tunnels/:id
pub async fn patch_tunnel(
    State(state): State<AppState>,
    AdminUser(admin): AdminUser,
    Path(id): Path<i64>,
    Json(body): Json<TunnelPatch>,
) -> R {
    let res = sqlx::query("UPDATE tunnels SET disabled = ?, updated_at = ? WHERE id = ?")
        .bind(body.disabled)
        .bind(util::now())
        .bind(id)
        .execute(&state.db)
        .await;
    match res {
        Ok(r) if r.rows_affected() > 0 => {
            crate::db::audit(
                &state.db,
                Some(&admin.email),
                "admin.tunnel_disable",
                Some(&id.to_string()),
                Some(&format!("disabled={}", body.disabled)),
                None,
            )
            .await;
            Json(json!({ "ok": true })).into_response()
        }
        _ => err(StatusCode::NOT_FOUND, "隧道不存在"),
    }
}

#[derive(Deserialize)]
pub struct AuditQuery {
    #[serde(default = "default_limit")]
    limit: i64,
}
fn default_limit() -> i64 {
    200
}

/// GET /api/admin/audit
pub async fn audit(State(state): State<AppState>, AdminUser(_admin): AdminUser, Query(q): Query<AuditQuery>) -> R {
    let limit = q.limit.clamp(1, 1000);
    let rows = sqlx::query_as::<_, AuditRow>("SELECT * FROM audit_log ORDER BY id DESC LIMIT ?")
        .bind(limit)
        .fetch_all(&state.db)
        .await
        .unwrap_or_default();
    Json(json!({ "audit": rows })).into_response()
}

pub fn routes() -> axum::Router<AppState> {
    use axum::routing::{get, patch};
    axum::Router::new()
        .route("/api/admin/overview", get(overview))
        .route("/api/admin/users", get(list_users))
        .route("/api/admin/users/:id", patch(patch_user))
        .route("/api/admin/tunnels", get(list_tunnels))
        .route("/api/admin/tunnels/:id", patch(patch_tunnel))
        .route("/api/admin/audit", get(audit))
}
