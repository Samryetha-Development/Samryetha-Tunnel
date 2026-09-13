use crate::db::Db;
use crate::util;
use std::collections::HashMap;
use std::sync::Arc;
use std::time::Instant;
use tokio::sync::Mutex;

/// 每用户流量/请求计数，内存累加 + 定时落库，避免每个分块都写 DB
#[derive(Clone)]
pub struct UsageMeter {
    db: Db,
    pending: Arc<Mutex<HashMap<i64, (i64, i64)>>>,
}

impl UsageMeter {
    pub fn new(db: Db) -> Self {
        Self {
            db,
            pending: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    pub async fn add(&self, user_id: i64, bytes: i64, requests: i64) {
        let mut g = self.pending.lock().await;
        let e = g.entry(user_id).or_insert((0, 0));
        e.0 += bytes.max(0);
        e.1 += requests.max(0);
    }

    /// 落库（后台每 5 秒调一次）
    pub async fn flush(&self) {
        let snapshot: Vec<(i64, i64, i64)> = {
            let mut g = self.pending.lock().await;
            let items = g.iter().map(|(k, v)| (*k, v.0, v.1)).collect();
            g.clear();
            items
        };
        if snapshot.is_empty() {
            return;
        }
        let day = util::today();
        for (user_id, bytes, requests) in snapshot {
            let _ = sqlx::query(
                "INSERT INTO usage_daily (user_id, day, bytes, requests) VALUES (?, ?, ?, ?) \
                 ON CONFLICT(user_id, day) DO UPDATE SET bytes = bytes + excluded.bytes, requests = requests + excluded.requests",
            )
            .bind(user_id)
            .bind(&day)
            .bind(bytes)
            .bind(requests)
            .execute(&self.db)
            .await;
        }
    }

    pub async fn today(&self, user_id: i64) -> (i64, i64) {
        let day = util::today();
        let row: Option<(i64, i64)> = sqlx::query_as(
            "SELECT bytes, requests FROM usage_daily WHERE user_id = ? AND day = ?",
        )
        .bind(user_id)
        .bind(&day)
        .fetch_optional(&self.db)
        .await
        .unwrap_or(None);
        let (mut b, mut r) = row.unwrap_or((0, 0));
        let g = self.pending.lock().await;
        if let Some((pb, pr)) = g.get(&user_id) {
            b += pb;
            r += pr;
        }
        (b, r)
    }
}

struct Bucket {
    tokens: f64,
    last: Instant,
}

/// 简单令牌桶限流：每用户 rate 次/秒，突发 burst
#[derive(Clone)]
pub struct RateLimiter {
    rate: f64,
    burst: f64,
    buckets: Arc<Mutex<HashMap<i64, Bucket>>>,
}

impl RateLimiter {
    pub fn new(rate_per_sec: f64, burst: f64) -> Self {
        Self {
            rate: rate_per_sec,
            burst,
            buckets: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    /// true = 放行
    pub async fn allow(&self, user_id: i64) -> bool {
        let now = Instant::now();
        let mut g = self.buckets.lock().await;
        let b = g.entry(user_id).or_insert(Bucket {
            tokens: self.burst,
            last: now,
        });
        let elapsed = now.duration_since(b.last).as_secs_f64();
        b.last = now;
        b.tokens = (b.tokens + elapsed * self.rate).min(self.burst);
        if b.tokens >= 1.0 {
            b.tokens -= 1.0;
            true
        } else {
            false
        }
    }
}
