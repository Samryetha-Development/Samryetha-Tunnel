use std::env;

#[derive(Clone, Debug, PartialEq)]
pub enum Role {
    Server,
    Relay,
}

#[derive(Clone, Debug, PartialEq)]
pub enum AuthMode {
    Oidc,
    Dev,
}

#[derive(Clone, Debug)]
pub struct OidcConfig {
    pub issuer: String,
    pub client_id: String,
    pub client_secret: String,
    pub redirect_uri: String,
    pub scopes: String,
    pub device_enabled: bool,
}

#[derive(Clone, Debug)]
pub struct Config {
    pub role: Role,
    pub bind: String,
    pub port: u16,
    pub base_domain: String,
    pub database_url: String,
    pub auth_mode: AuthMode,
    pub oidc: Option<OidcConfig>,
    pub admin_emails: Vec<String>,
    pub default_max_tunnels: i64,
    pub default_daily_bytes: i64,
    pub tcp_port_start: u16,
    pub tcp_port_end: u16,
    pub relay_host: String,
    pub cookie_secure: bool,
    pub max_streams_per_user: i64,
    pub rate_per_sec: f64,
    pub rate_burst: f64,
}

fn var(key: &str, default: &str) -> String {
    env::var(key).unwrap_or_else(|_| default.to_string())
}

fn var_opt(key: &str) -> Option<String> {
    env::var(key).ok().filter(|v| !v.trim().is_empty())
}

impl Config {
    pub fn from_env() -> Self {
        let role = match var("ROLE", "server").to_lowercase().as_str() {
            "relay" => Role::Relay,
            _ => Role::Server,
        };
        let auth_mode = match var("AUTH_MODE", "dev").to_lowercase().as_str() {
            "oidc" => AuthMode::Oidc,
            _ => AuthMode::Dev,
        };
        let oidc = if auth_mode == AuthMode::Oidc {
            let issuer = var_opt("OIDC_ISSUER").expect("AUTH_MODE=oidc 时必须设置 OIDC_ISSUER");
            Some(OidcConfig {
                issuer: issuer.trim_end_matches('/').to_string(),
                client_id: var_opt("OIDC_CLIENT_ID").expect("缺少 OIDC_CLIENT_ID"),
                client_secret: var("OIDC_CLIENT_SECRET", ""),
                redirect_uri: var_opt("OIDC_REDIRECT_URI")
                    .expect("缺少 OIDC_REDIRECT_URI（必须与 IdP 登记逐字符一致）"),
                scopes: var("OIDC_SCOPES", "openid profile email"),
                device_enabled: var("OIDC_DEVICE_ENABLED", "true") == "true",
            })
        } else {
            None
        };

        let admin_emails = var("ADMIN_EMAILS", "")
            .split(',')
            .map(|s| s.trim().to_lowercase())
            .filter(|s| !s.is_empty())
            .collect();

        Config {
            role,
            bind: var("BIND", "127.0.0.1"),
            port: var("PORT", "18080").parse().unwrap_or(18080),
            base_domain: var("BASE_DOMAIN", "frp.example.com").to_lowercase(),
            database_url: var("DATABASE_URL", "sqlite://tunnel.db?mode=rwc"),
            auth_mode,
            oidc,
            admin_emails,
            default_max_tunnels: var("DEFAULT_MAX_TUNNELS", "5").parse().unwrap_or(5),
            default_daily_bytes: var("DEFAULT_DAILY_BYTES", "10737418240").parse().unwrap_or(10_737_418_240),
            tcp_port_start: var("TCP_PORT_START", "10000").parse().unwrap_or(10000),
            tcp_port_end: var("TCP_PORT_END", "10099").parse().unwrap_or(10099),
            relay_host: var("RELAY_HOST", "47.103.21.5"),
            cookie_secure: var("COOKIE_SECURE", "true") == "true",
            max_streams_per_user: var("MAX_STREAMS_PER_USER", "64").parse().unwrap_or(64),
            rate_per_sec: var("RATE_PER_SEC", "50").parse().unwrap_or(50.0),
            rate_burst: var("RATE_BURST", "100").parse().unwrap_or(100.0),
        }
    }

    /// 域名是否属于该用户的命名空间（子域名前缀校验用）
    pub fn uses_oidc(&self) -> bool {
        self.auth_mode == AuthMode::Oidc
    }
}
