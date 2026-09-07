mod control;
mod protocol;
mod registry;
mod visitor;

use axum::{
    extract::{ws::WebSocketUpgrade, State},
    http::{HeaderMap, StatusCode},
    response::IntoResponse,
    routing::get,
    Router,
};
use std::net::SocketAddr;
use tracing::warn;

#[derive(Clone)]
pub struct AppState {
    pub registry: registry::Registry,
    pub token: String,
    pub base_domain: String,
}

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .init();

    let port: u16 = std::env::var("PORT").ok().and_then(|v| v.parse().ok()).unwrap_or(18080);
    let token = std::env::var("SERVER_TOKEN").unwrap_or_else(|_| "change-me".to_string());
    let base_domain =
        std::env::var("BASE_DOMAIN").unwrap_or_else(|_| "frp.example.com".to_string());
    if token == "change-me" {
        warn!("SERVER_TOKEN 未设置，用的默认值 change-me，生产务必改掉！");
    }

    let state = AppState {
        registry: registry::Registry::new(),
        token,
        base_domain,
    };

    let app = Router::new()
        .route("/healthz", get(|| async { "ok" }))
        .route("/tunnel", get(tunnel_handler))
        // 访客 HTTP：其余全部走分发
        .fallback(visitor::visitor_handler)
        .with_state(state);

    let addr = SocketAddr::from(([127, 0, 0, 1], port));
    println!("tunnel-server listening on http://{addr} (behind Caddy)");
    let listener = tokio::net::TcpListener::bind(addr).await.unwrap();
    axum::serve(
        listener,
        app.into_make_service_with_connect_info::<SocketAddr>(),
    )
    .await
    .unwrap();
}

/// 控制通道：GET /tunnel 升级 WS，需 Authorization: Bearer <token>
async fn tunnel_handler(
    State(state): State<AppState>,
    headers: HeaderMap,
    ws: WebSocketUpgrade,
) -> impl IntoResponse {
    let auth = headers
        .get("authorization")
        .and_then(|v| v.to_str().ok())
        .unwrap_or("")
        .to_string();
    if auth != format!("Bearer {}", state.token) {
        return (StatusCode::UNAUTHORIZED, "bad token").into_response();
    }
    let registry = state.registry.clone();
    ws.on_upgrade(move |socket| control::handle_socket(socket, registry))
        .into_response()
}
