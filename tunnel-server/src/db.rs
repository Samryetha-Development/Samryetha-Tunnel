use sqlx::sqlite::{SqliteConnectOptions, SqlitePoolOptions};
use sqlx::SqlitePool;
use std::str::FromStr;

pub type Db = SqlitePool;

const SCHEMA: &str = r#"
CREATE TABLE IF NOT EXISTS users (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  email TEXT NOT NULL UNIQUE,
  name TEXT,
  slug TEXT NOT NULL,
  role TEXT NOT NULL DEFAULT 'user',
  max_tunnels INTEGER NOT NULL DEFAULT 5,
  daily_bytes INTEGER NOT NULL DEFAULT 10737418240,
  disabled INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_users_slug ON users(slug);

CREATE TABLE IF NOT EXISTS sessions (
  id TEXT PRIMARY KEY,
  user_id INTEGER NOT NULL,
  created_at TEXT NOT NULL,
  expires_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_sessions_user ON sessions(user_id);

CREATE TABLE IF NOT EXISTS api_tokens (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL,
  name TEXT NOT NULL DEFAULT 'default',
  token_hash TEXT NOT NULL UNIQUE,
  prefix TEXT NOT NULL,
  created_at TEXT NOT NULL,
  last_used_at TEXT,
  revoked INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_tokens_user ON api_tokens(user_id);

CREATE TABLE IF NOT EXISTS tunnels (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL,
  tunnel_id TEXT NOT NULL,
  proto TEXT NOT NULL DEFAULT 'http',
  public_host TEXT NOT NULL DEFAULT '',
  path_prefix TEXT,
  local_addr TEXT,
  visitor_auth TEXT,
  disabled INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  UNIQUE(user_id, tunnel_id)
);

CREATE TABLE IF NOT EXISTS usage_daily (
  user_id INTEGER NOT NULL,
  day TEXT NOT NULL,
  bytes INTEGER NOT NULL DEFAULT 0,
  requests INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(user_id, day)
);

CREATE TABLE IF NOT EXISTS audit_log (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  at TEXT NOT NULL,
  actor TEXT,
  action TEXT NOT NULL,
  target TEXT,
  detail TEXT,
  ip TEXT
);
CREATE INDEX IF NOT EXISTS idx_audit_at ON audit_log(at);
"#;

pub async fn init(database_url: &str) -> Result<Db, sqlx::Error> {
    let opts = SqliteConnectOptions::from_str(database_url)?
        .create_if_missing(true)
        .journal_mode(sqlx::sqlite::SqliteJournalMode::Wal)
        .foreign_keys(true);
    let pool = SqlitePoolOptions::new()
        .max_connections(8)
        .connect_with(opts)
        .await?;
    sqlx::raw_sql(SCHEMA).execute(&pool).await?;
    Ok(pool)
}

pub async fn upsert_user(
    db: &Db,
    email: &str,
    name: Option<&str>,
    is_admin: bool,
    default_max_tunnels: i64,
    default_daily_bytes: i64,
) -> Result<super::models::User, sqlx::Error> {
    let email = email.to_lowercase();
    let existing = sqlx::query_as::<_, super::models::User>("SELECT * FROM users WHERE email = ?")
        .bind(&email)
        .fetch_optional(db)
        .await?;

    if let Some(mut u) = existing {
        // 管理员名单是权威来源：命中则升，admin_emails 里没有也不降级（手动授权保留）
        if is_admin && !u.is_admin() {
            sqlx::query("UPDATE users SET role = 'admin' WHERE id = ?")
                .bind(u.id)
                .execute(db)
                .await?;
            u.role = "admin".to_string();
        }
        if let Some(n) = name {
            if u.name.as_deref() != Some(n) {
                sqlx::query("UPDATE users SET name = ? WHERE id = ?")
                    .bind(n)
                    .bind(u.id)
                    .execute(db)
                    .await?;
                u.name = Some(n.to_string());
            }
        }
        return Ok(u);
    }

    // 生成唯一 slug
    let mut slug = super::util::slugify(&email);
    let mut n = 1;
    loop {
        let taken: Option<(i64,)> = sqlx::query_as("SELECT id FROM users WHERE slug = ?")
            .bind(&slug)
            .fetch_optional(db)
            .await?;
        if taken.is_none() {
            break;
        }
        n += 1;
        slug = format!("{}-{}", super::util::slugify(&email), n);
    }

    let role = if is_admin { "admin" } else { "user" };
    let created = super::util::now();
    let res = sqlx::query(
        "INSERT INTO users (email, name, slug, role, max_tunnels, daily_bytes, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)",
    )
    .bind(&email)
    .bind(name)
    .bind(&slug)
    .bind(role)
    .bind(default_max_tunnels)
    .bind(default_daily_bytes)
    .bind(&created)
    .execute(db)
    .await?;

    sqlx::query_as::<_, super::models::User>("SELECT * FROM users WHERE id = ?")
        .bind(res.last_insert_rowid())
        .fetch_one(db)
        .await
}

pub async fn audit(db: &Db, actor: Option<&str>, action: &str, target: Option<&str>, detail: Option<&str>, ip: Option<&str>) {
    let _ = sqlx::query("INSERT INTO audit_log (at, actor, action, target, detail, ip) VALUES (?, ?, ?, ?, ?, ?)")
        .bind(super::util::now())
        .bind(actor)
        .bind(action)
        .bind(target)
        .bind(detail)
        .bind(ip)
        .execute(db)
        .await;
}
