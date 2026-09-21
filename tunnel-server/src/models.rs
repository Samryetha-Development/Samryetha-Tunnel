use serde::{Deserialize, Serialize};
use sqlx::sqlite::SqliteRow;
use sqlx::{FromRow, Row};

/// 手写 FromRow（不依赖 sqlx 编译期宏，避免 proc-macro dylib，也让构建更快）
macro_rules! row_from_sqlite {
    ($t:ident { $($f:ident : $ty:ty),* $(,)? }) => {
        impl<'r> FromRow<'r, SqliteRow> for $t {
            fn from_row(row: &'r SqliteRow) -> Result<Self, sqlx::Error> {
                Ok($t { $( $f: row.try_get(stringify!($f))?, )* })
            }
        }
    };
}

#[derive(Debug, Clone)]
pub struct User {
    pub id: i64,
    pub email: String,
    pub name: Option<String>,
    pub slug: String,
    pub role: String,
    pub max_tunnels: i64,
    pub daily_bytes: i64,
    pub disabled: bool,
    pub created_at: String,
}

row_from_sqlite!(User {
    id: i64,
    email: String,
    name: Option<String>,
    slug: String,
    role: String,
    max_tunnels: i64,
    daily_bytes: i64,
    disabled: bool,
    created_at: String,
});

impl User {
    pub fn is_admin(&self) -> bool {
        self.role == "admin"
    }
}

#[derive(Debug, Clone, Serialize)]
#[allow(dead_code)]
pub struct ApiToken {
    pub id: i64,
    #[serde(skip)]
    pub user_id: i64,
    pub name: String,
    #[serde(skip)]
    pub token_hash: String,
    pub prefix: String,
    pub created_at: String,
    pub last_used_at: Option<String>,
    pub revoked: bool,
}

row_from_sqlite!(ApiToken {
    id: i64,
    user_id: i64,
    name: String,
    token_hash: String,
    prefix: String,
    created_at: String,
    last_used_at: Option<String>,
    revoked: bool,
});

/// 一条已注册的隧道（持久化配置，实时路由另在 registry 内存里）
#[derive(Debug, Clone, Serialize)]
pub struct TunnelRow {
    pub id: i64,
    #[serde(skip)]
    pub user_id: i64,
    pub tunnel_id: String,
    pub proto: String,
    pub public_host: String,
    pub path_prefix: Option<String>,
    pub local_addr: Option<String>,
    pub visitor_auth: Option<String>,
    pub disabled: bool,
    pub created_at: String,
    pub updated_at: String,
}

row_from_sqlite!(TunnelRow {
    id: i64,
    user_id: i64,
    tunnel_id: String,
    proto: String,
    public_host: String,
    path_prefix: Option<String>,
    local_addr: Option<String>,
    visitor_auth: Option<String>,
    disabled: bool,
    created_at: String,
    updated_at: String,
});

/// 客户端注册时上报的一条隧道
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TunnelDef {
    pub tunnel_id: String,
    #[serde(default)]
    pub proto: Option<String>, // http（默认）| tcp
    #[serde(default)]
    pub subdomain: Option<String>,
    #[serde(default)]
    pub path_prefix: Option<String>,
    #[serde(default)]
    pub local_addr: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct VisitorAuth {
    #[serde(default)]
    pub basic: Option<BasicAuth>,
    #[serde(default)]
    pub ips: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BasicAuth {
    pub user: String,
    pub pass: String,
}

#[derive(Debug, Clone, Serialize)]
pub struct AuditRow {
    pub id: i64,
    pub at: String,
    pub actor: Option<String>,
    pub action: String,
    pub target: Option<String>,
    pub detail: Option<String>,
    pub ip: Option<String>,
}

row_from_sqlite!(AuditRow {
    id: i64,
    at: String,
    actor: Option<String>,
    action: String,
    target: Option<String>,
    detail: Option<String>,
    ip: Option<String>,
});

#[derive(Debug, Clone, Serialize)]
pub struct UsageRow {
    pub day: String,
    pub bytes: i64,
    pub requests: i64,
}

row_from_sqlite!(UsageRow {
    day: String,
    bytes: i64,
    requests: i64,
});
