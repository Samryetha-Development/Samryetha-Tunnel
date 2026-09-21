mod auth;
mod binproto;
mod config;
mod db;
mod mgmt;
mod models;
mod protocol;
mod quota;
mod registry;
mod relay;
mod routes_admin;
mod routes_api;
mod routes_auth;
mod routes_visitor;
mod routes_ws;
mod util;

use axum::routing::get;
use axum::Router;
use config::Role;
use std::net::SocketAddr;
use std::sync::Arc;
use tracing::{info, warn};

#[tokio::main]
async fn main() {
    let _ = dotenvy::dotenv();
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("info")),
        )
        .init();

    let cfg = config::Config::from_env();
    let db = db::init(&cfg.database_url)
        .await
        .expect("数据库初始化失败");

    let http = reqwest::Client::builder()
        .timeout(std::time::Duration::from_secs(15))
        .build()
        .expect("http client");

    // OIDC discovery（失败不阻塞，登录时提示）
    let discovery = if cfg.uses_oidc() {
        match auth::load_discovery(&cfg, &http).await {
            Ok(d) => {
                info!("OIDC discovery OK: issuer={}", d.issuer);
                Some(d)
            }
            Err(e) => {
                warn!("OIDC discovery 失败（登录前请检查 OIDC_ISSUER）: {e}");
                None
            }
        }
    } else {
        None
    };

    let registry = registry::Registry::new();
    let usage = quota::UsageMeter::new(db.clone());
    let limiter = quota::RateLimiter::new(cfg.rate_per_sec, cfg.rate_burst);

    let state = auth::AppState {
        cfg: cfg.clone(),
        db: db.clone(),
        registry: registry.clone(),
        http,
        oidc: Arc::new(auth::OidcRuntime {
            discovery,
            states: tokio::sync::Mutex::new(std::collections::HashMap::new()),
            jwks: tokio::sync::RwLock::new(None),
        }),
        usage: usage.clone(),
        limiter,
        streams: Arc::new(tokio::sync::Mutex::new(std::collections::HashMap::new())),
    };

    // 用量定时落库
    {
        let usage = usage.clone();
        tokio::spawn(async move {
            let mut ticker = tokio::time::interval(std::time::Duration::from_secs(5));
            loop {
                ticker.tick().await;
                usage.flush().await;
            }
        });
    }

    // 公共路由：健康检查 / 认证 / 控制台 / 控制通道
    let mut app = Router::new()
        .route("/healthz", get(|| async { "ok" }))
        .route("/tunnel", get(routes_ws::tunnel_handler))
        .merge(routes_auth::routes())
        .merge(routes_api::routes())
        .merge(routes_admin::routes());

    match cfg.role {
        Role::Server => {
            app = app.fallback(routes_visitor::visitor_handler);
        }
        Role::Relay => {
            let state2 = state.clone();
            tokio::spawn(async move { relay::run(state2).await });
        }
    }

    let app = app.with_state(state);
    let addr: SocketAddr = match cfg.role {
        Role::Server => format!("{}:{}", cfg.bind, cfg.port),
        Role::Relay => format!("0.0.0.0:{}", cfg.port),
    }
    .parse()
    .unwrap();

    match cfg.role {
        Role::Server => info!(
            "tunnel-server(role=server) listening http://{addr} base_domain={} auth={}",
            cfg.base_domain,
            auth::auth_mode_name(&cfg)
        ),
        Role::Relay => info!(
            "tunnel-server(role=relay) listening ws://{addr}/tunnel, tcp pool {}-{}",
            cfg.tcp_port_start, cfg.tcp_port_end
        ),
    }

    let listener = tokio::net::TcpListener::bind(addr).await.unwrap();
    axum::serve(
        listener,
        app.into_make_service_with_connect_info::<SocketAddr>(),
    )
    .tcp_nodelay(true)
    .await
    .unwrap();
}
