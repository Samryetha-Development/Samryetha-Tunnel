//! 管理分发器：与 HTTP 管理 API 等价，但通过控制通道（WebSocket）调用。
//! 这样客户端只连 /tunnel 一条通道，服务端无需暴露 /api。

use crate::auth::{self, AppState};
use crate::models::{TunnelRow, User, VisitorAuth};
use crate::util;
use serde_json::{json, Value};

const OK: u16 = 200;
const BAD: u16 = 400;
const UNAUTHORIZED: u16 = 401;
const FORBIDDEN: u16 = 403;
const NOTFOUND: u16 = 404;

/// 未认证连接只允许这些路径（用于首次登录引导）
pub fn anon_allowed(path: &str) -> bool {
    path == "/auth/device/start" || path == "/auth/device/poll" || path == "/auth/config"
}

fn split(path: &str) -> (String, Option<String>, Option<String>) {
    let (p, query) = match path.split_once('?') {
        Some((a, b)) => (a.to_string(), Some(b.to_string())),
        None => (path.to_string(), None),
    };
    // 仅这些前缀的最后一段视为参数
    for prefix in [
        "/api/admin/users/",
        "/api/admin/tunnels/",
        "/api/tokens/",
        "/api/tunnels/",
    ] {
        if let Some(rest) = p.strip_prefix(prefix) {
            if !rest.is_empty() && !rest.contains('/') {
                let base = prefix.trim_end_matches('/').to_string();
                return (base, Some(rest.to_string()), query);
            }
        }
    }
    (p, None, query)
}

fn tunnel_public_url(app: &AppState, t: &TunnelRow) -> String {
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

fn tunnel_json(app: &AppState, t: &TunnelRow, online: bool) -> Value {
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

fn user_json(u: &User, online: bool) -> Value {
    json!({
        "id": u.id, "email": u.email, "name": u.name, "slug": u.slug,
        "role": u.role, "max_tunnels": u.max_tunnels, "daily_bytes": u.daily_bytes,
        "disabled": u.disabled, "online": online, "created_at": u.created_at,
    })
}

/// 返回 (status, body_json)
pub async fn handle(
    state: &AppState,
    user: Option<&User>,
    method: &str,
    path: &str,
    body: &[u8],
) -> (u16, Value) {
    let (base, arg, query) = split(path);
    let method = method.to_uppercase();
    let req: Value = if body.is_empty() {
        json!({})
    } else {
        serde_json::from_slice(body).unwrap_or(json!({}))
    };

    // ---------- 无需登录 ----------
    if base == "/auth/config" {
        return (
            OK,
            json!({
                "auth_mode": auth::auth_mode_name(&state.cfg),
                "base_domain": state.cfg.base_domain,
                "device_enabled": state.cfg.oidc.as_ref().map(|o| o.device_enabled).unwrap_or(false),
                "role": match state.cfg.role {
                    crate::config::Role::Server => "server",
                    crate::config::Role::Relay => "relay",
                },
            }),
        );
    }
    if base == "/auth/device/start" {
        return match auth::device_start(state).await {
            Ok(d) => (
                OK,
                json!({
                    "device_code": d.device_code,
                    "user_code": d.user_code,
                    "verification_uri": d.verification_uri,
                    "verification_uri_complete": d.verification_uri_complete,
                    "expires_in": d.expires_in,
                    "interval": if d.interval > 0 { d.interval } else { 5 },
                }),
            ),
            Err(e) => (BAD, json!({ "error": e })),
        };
    }
    if base == "/auth/device/poll" {
        let code = req.get("device_code").and_then(|v| v.as_str()).unwrap_or("");
        if code.is_empty() {
            return (BAD, json!({ "error": "缺少 device_code" }));
        }
        let tr = match auth::device_poll(state, code).await {
            Ok(t) => t,
            Err(e) => return (BAD, json!({ "error": e })),
        };
        if let Some(err) = &tr.error {
            let pending = matches!(err.as_str(), "authorization_pending" | "slow_down");
            return (
                OK,
                json!({
                    "status": if pending { "pending" } else { "error" },
                    "error": err,
                    "description": tr.error_description,
                }),
            );
        }
        let Some(id_token) = tr.id_token else {
            return (UNAUTHORIZED, json!({ "error": "IdP 未返回 id_token" }));
        };
        let claims = match auth::verify_id_token(state, &id_token).await {
            Ok(c) => c,
            Err(e) => return (UNAUTHORIZED, json!({ "error": e })),
        };
        let u = match auth::upsert_from_claims(state, claims.email.as_deref(), claims.name.as_deref()).await {
            Ok(u) => u,
            Err(e) => return (UNAUTHORIZED, json!({ "error": e })),
        };
        let (raw, prefix) = match auth::create_api_token(&state.db, u.id, "device").await {
            Ok(v) => v,
            Err(e) => return (BAD, json!({ "error": e.to_string() })),
        };
        crate::db::audit(&state.db, Some(&u.email), "auth.device_login", Some(&prefix), None, None).await;
        return (
            OK,
            json!({
                "status": "ok", "token": raw, "prefix": prefix,
                "user": {"id": u.id, "email": u.email, "slug": u.slug, "role": u.role}
            }),
        );
    }

    // ---------- 需要登录 ----------
    let Some(u) = user else {
        return (UNAUTHORIZED, json!({ "error": "未登录" }));
    };

    match (method.as_str(), base.as_str()) {
        ("GET", "/auth/me") => (
            OK,
            json!({
                "id": u.id, "email": u.email, "name": u.name, "slug": u.slug,
                "role": u.role, "max_tunnels": u.max_tunnels, "daily_bytes": u.daily_bytes,
            }),
        ),

        // ----- Token -----
        ("GET", "/api/tokens") => {
            let rows = sqlx::query_as::<_, crate::models::ApiToken>(
                "SELECT * FROM api_tokens WHERE user_id = ? AND revoked = 0 ORDER BY created_at DESC",
            )
            .bind(u.id)
            .fetch_all(&state.db)
            .await
            .unwrap_or_default();
            (OK, json!({ "tokens": rows }))
        }
        ("POST", "/api/tokens") => {
            let name = req.get("name").and_then(|v| v.as_str()).unwrap_or("default");
            match auth::create_api_token(&state.db, u.id, name).await {
                Ok((raw, prefix)) => {
                    crate::db::audit(&state.db, Some(&u.email), "token.create", Some(&prefix), Some(name), None).await;
                    (OK, json!({ "token": raw, "prefix": prefix, "name": name }))
                }
                Err(e) => (BAD, json!({ "error": e.to_string() })),
            }
        }
        ("DELETE", "/api/tokens") => {
            let id: i64 = arg.as_deref().and_then(|s| s.parse().ok()).unwrap_or(0);
            let r = sqlx::query("UPDATE api_tokens SET revoked = 1 WHERE id = ? AND user_id = ?")
                .bind(id)
                .bind(u.id)
                .execute(&state.db)
                .await;
            match r {
                Ok(x) if x.rows_affected() > 0 => {
                    crate::db::audit(&state.db, Some(&u.email), "token.revoke", Some(&id.to_string()), None, None).await;
                    (OK, json!({ "ok": true }))
                }
                _ => (NOTFOUND, json!({ "error": "令牌不存在" })),
            }
        }

        // ----- 我的隧道 -----
        ("GET", "/api/tunnels") => {
            let rows = sqlx::query_as::<_, TunnelRow>(
                "SELECT * FROM tunnels WHERE user_id = ? ORDER BY created_at",
            )
            .bind(u.id)
            .fetch_all(&state.db)
            .await
            .unwrap_or_default();
            let online = state.registry.online_users().await.contains(&u.id);
            let items: Vec<Value> = rows.iter().map(|t| tunnel_json(state, t, online)).collect();
            (OK, json!({ "tunnels": items }))
        }
        ("PATCH", "/api/tunnels") => {
            let tid = arg.unwrap_or_default();
            let row: Option<TunnelRow> =
                sqlx::query_as("SELECT * FROM tunnels WHERE user_id = ? AND tunnel_id = ?")
                    .bind(u.id)
                    .bind(&tid)
                    .fetch_optional(&state.db)
                    .await
                    .unwrap_or(None);
            let Some(row) = row else {
                return (NOTFOUND, json!({ "error": "隧道不存在" }));
            };
            if let Some(d) = req.get("disabled").and_then(|v| v.as_bool()) {
                let _ = sqlx::query("UPDATE tunnels SET disabled = ?, updated_at = ? WHERE id = ?")
                    .bind(d)
                    .bind(util::now())
                    .bind(row.id)
                    .execute(&state.db)
                    .await;
            }
            if let Some(va) = req.get("visitor_auth") {
                let s = serde_json::to_string(va).unwrap_or_else(|_| "{}".into());
                let _ = sqlx::query("UPDATE tunnels SET visitor_auth = ?, updated_at = ? WHERE id = ?")
                    .bind(&s)
                    .bind(util::now())
                    .bind(row.id)
                    .execute(&state.db)
                    .await;
            }
            crate::db::audit(&state.db, Some(&u.email), "tunnel.update", Some(&tid), None, None).await;
            (OK, json!({ "ok": true, "note": "重连客户端后生效" }))
        }
        ("DELETE", "/api/tunnels") => {
            let tid = arg.unwrap_or_default();
            let r = sqlx::query("DELETE FROM tunnels WHERE user_id = ? AND tunnel_id = ?")
                .bind(u.id)
                .bind(&tid)
                .execute(&state.db)
                .await;
            match r {
                Ok(x) if x.rows_affected() > 0 => {
                    crate::db::audit(&state.db, Some(&u.email), "tunnel.delete", Some(&tid), None, None).await;
                    (OK, json!({ "ok": true }))
                }
                _ => (NOTFOUND, json!({ "error": "隧道不存在" })),
            }
        }
        ("GET", "/api/usage") => {
            let day = util::today();
            let row: Option<crate::models::UsageRow> = sqlx::query_as(
                "SELECT day, bytes, requests FROM usage_daily WHERE user_id = ? AND day = ?",
            )
            .bind(u.id)
            .bind(&day)
            .fetch_optional(&state.db)
            .await
            .unwrap_or(None);
            let (bytes, requests) = row.map(|r| (r.bytes, r.requests)).unwrap_or((0, 0));
            (
                OK,
                json!({ "day": day, "bytes": bytes, "requests": requests, "daily_bytes_limit": u.daily_bytes }),
            )
        }

        // ----- 管理员 -----
        _ if base.starts_with("/api/admin") => {
            if !u.is_admin() {
                return (FORBIDDEN, json!({ "error": "需要管理员权限" }));
            }
            admin(state, u, &method, &base, arg.as_deref(), query.as_deref(), &req).await
        }

        _ => (NOTFOUND, json!({ "error": format!("未知管理路径 {method} {base}") })),
    }
}

async fn admin(
    state: &AppState,
    admin: &User,
    method: &str,
    base: &str,
    arg: Option<&str>,
    query: Option<&str>,
    req: &Value,
) -> (u16, Value) {
    match (method, base) {
        ("GET", "/api/admin/overview") => {
            let users: i64 = sqlx::query_scalar("SELECT COUNT(*) FROM users").fetch_one(&state.db).await.unwrap_or(0);
            let tunnels: i64 = sqlx::query_scalar("SELECT COUNT(*) FROM tunnels").fetch_one(&state.db).await.unwrap_or(0);
            let online = state.registry.online_users().await.len();
            let requests: i64 = sqlx::query_scalar("SELECT COALESCE(SUM(requests),0) FROM usage_daily")
                .fetch_one(&state.db)
                .await
                .unwrap_or(0);
            (OK, json!({ "users": users, "tunnels": tunnels, "online_clients": online, "requests_today": requests }))
        }
        ("GET", "/api/admin/users") => {
            let users = sqlx::query_as::<_, User>("SELECT * FROM users ORDER BY created_at")
                .fetch_all(&state.db)
                .await
                .unwrap_or_default();
            let online = state.registry.online_users().await;
            let items: Vec<Value> = users.iter().map(|x| user_json(x, online.contains(&x.id))).collect();
            (OK, json!({ "users": items }))
        }
        ("PATCH", "/api/admin/users") => {
            let id: i64 = arg.and_then(|s| s.parse().ok()).unwrap_or(0);
            if id == admin.id && req.get("disabled").and_then(|v| v.as_bool()) == Some(true) {
                return (BAD, json!({ "error": "不能停用自己" }));
            }
            let exists: Option<(i64,)> = sqlx::query_as("SELECT id FROM users WHERE id = ?")
                .bind(id)
                .fetch_optional(&state.db)
                .await
                .unwrap_or(None);
            if exists.is_none() {
                return (NOTFOUND, json!({ "error": "用户不存在" }));
            }
            if let Some(role) = req.get("role").and_then(|v| v.as_str()) {
                if role != "admin" && role != "user" {
                    return (BAD, json!({ "error": "role 只能是 admin/user" }));
                }
                let _ = sqlx::query("UPDATE users SET role = ? WHERE id = ?").bind(role).bind(id).execute(&state.db).await;
            }
            if let Some(d) = req.get("disabled").and_then(|v| v.as_bool()) {
                let _ = sqlx::query("UPDATE users SET disabled = ? WHERE id = ?").bind(d).bind(id).execute(&state.db).await;
                if d {
                    state.registry.remove_user_conns(id).await;
                }
            }
            if let Some(n) = req.get("max_tunnels").and_then(|v| v.as_i64()) {
                let _ = sqlx::query("UPDATE users SET max_tunnels = ? WHERE id = ?").bind(n).bind(id).execute(&state.db).await;
            }
            if let Some(n) = req.get("daily_bytes").and_then(|v| v.as_i64()) {
                let _ = sqlx::query("UPDATE users SET daily_bytes = ? WHERE id = ?").bind(n).bind(id).execute(&state.db).await;
            }
            crate::db::audit(&state.db, Some(&admin.email), "admin.user_update", Some(&id.to_string()), None, None).await;
            (OK, json!({ "ok": true }))
        }
        ("GET", "/api/admin/tunnels") => {
            let rows: Vec<TunnelRow> = sqlx::query_as("SELECT * FROM tunnels ORDER BY created_at DESC")
                .fetch_all(&state.db)
                .await
                .unwrap_or_default();
            let online = state.registry.online_users().await;
            let items: Vec<Value> = rows
                .iter()
                .map(|t| {
                    json!({
                        "id": t.id, "user_id": t.user_id, "tunnel_id": t.tunnel_id, "proto": t.proto,
                        "public_url": tunnel_public_url(state, t), "disabled": t.disabled,
                        "online": online.contains(&t.user_id),
                    })
                })
                .collect();
            (OK, json!({ "tunnels": items }))
        }
        ("PATCH", "/api/admin/tunnels") => {
            let id: i64 = arg.and_then(|s| s.parse().ok()).unwrap_or(0);
            let d = req.get("disabled").and_then(|v| v.as_bool()).unwrap_or(false);
            let r = sqlx::query("UPDATE tunnels SET disabled = ?, updated_at = ? WHERE id = ?")
                .bind(d)
                .bind(util::now())
                .bind(id)
                .execute(&state.db)
                .await;
            match r {
                Ok(x) if x.rows_affected() > 0 => {
                    crate::db::audit(&state.db, Some(&admin.email), "admin.tunnel_disable", Some(&id.to_string()), None, None).await;
                    (OK, json!({ "ok": true }))
                }
                _ => (NOTFOUND, json!({ "error": "隧道不存在" })),
            }
        }
        ("GET", "/api/admin/audit") => {
            let limit: i64 = query
                .and_then(|q| q.split('&').find_map(|kv| kv.strip_prefix("limit=")))
                .and_then(|v| v.parse().ok())
                .unwrap_or(100)
                .clamp(1, 1000);
            let rows = sqlx::query_as::<_, crate::models::AuditRow>(
                "SELECT * FROM audit_log ORDER BY id DESC LIMIT ?",
            )
            .bind(limit)
            .fetch_all(&state.db)
            .await
            .unwrap_or_default();
            (OK, json!({ "audit": rows }))
        }
        _ => (NOTFOUND, json!({ "error": "未知管理员路径" })),
    }
}

/// 供 HTTP 侧复用
pub fn _unused(_: VisitorAuth) {}
