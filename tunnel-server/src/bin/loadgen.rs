// 极简压测器：并发发 HTTP 请求，统计 p50/p90/p99/p99.9、均值、抖动与吞吐
// 用法: loadgen <host> <port> <path> <concurrency> <total> [host_header]
use std::io::{Read, Write};
use std::net::TcpStream;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Instant;

fn main() {
    let a: Vec<String> = std::env::args().collect();
    if a.len() < 6 {
        eprintln!("usage: loadgen <host> <port> <path> <concurrency> <total> [host_header]");
        std::process::exit(2);
    }
    let host = a[1].clone();
    let port: u16 = a[2].parse().unwrap();
    let path = a[3].clone();
    let concurrency: usize = a[4].parse().unwrap();
    let total: usize = a[5].parse().unwrap();
    let host_header = a.get(6).cloned().unwrap_or_else(|| format!("{host}:{port}"));

    let lat = Arc::new(Mutex::new(Vec::<u64>::with_capacity(total)));
    let bytes = Arc::new(AtomicU64::new(0));
    let errors = Arc::new(AtomicU64::new(0));
    let per = total / concurrency;
    let req = format!(
        "GET {path} HTTP/1.1\r\nHost: {host_header}\r\nConnection: close\r\n\r\n"
    );

    let t0 = Instant::now();
    let mut handles = Vec::new();
    for _ in 0..concurrency {
        let (host, port, req) = (host.clone(), port, req.clone());
        let lat = lat.clone();
        let bytes = bytes.clone();
        let errors = errors.clone();
        handles.push(std::thread::spawn(move || {
            let mut local = Vec::with_capacity(per);
            let mut buf = [0u8; 16384];
            for _ in 0..per {
                let start = Instant::now();
                match TcpStream::connect((host.as_str(), port)) {
                    Ok(mut s) => {
                        let _ = s.set_nodelay(true);
                        if s.write_all(req.as_bytes()).is_ok() {
                            let mut got: u64 = 0;
                            loop {
                                match s.read(&mut buf) {
                                    Ok(0) => break,
                                    Ok(n) => got += n as u64,
                                    Err(_) => break,
                                }
                            }
                            bytes.fetch_add(got, Ordering::Relaxed);
                            local.push(start.elapsed().as_micros() as u64);
                        } else {
                            errors.fetch_add(1, Ordering::Relaxed);
                        }
                    }
                    Err(_) => {
                        errors.fetch_add(1, Ordering::Relaxed);
                    }
                }
            }
            lat.lock().unwrap().extend(local);
        }));
    }
    for h in handles {
        let _ = h.join();
    }
    let elapsed = t0.elapsed().as_secs_f64();

    let mut v = Arc::try_unwrap(lat).unwrap().into_inner().unwrap();
    v.sort_unstable();
    let n = v.len();
    if n == 0 {
        eprintln!("no samples");
        std::process::exit(1);
    }
    let pct = |p: f64| -> f64 {
        let idx = ((p / 100.0) * (n as f64 - 1.0)).round() as usize;
        v[idx.min(n - 1)] as f64 / 1000.0
    };
    let mean = v.iter().sum::<u64>() as f64 / n as f64 / 1000.0;
    let var = v
        .iter()
        .map(|x| {
            let d = *x as f64 / 1000.0 - mean;
            d * d
        })
        .sum::<f64>()
        / n as f64;
    let stddev = var.sqrt();
    let max = *v.last().unwrap() as f64 / 1000.0;
    let rps = n as f64 / elapsed;
    let total_bytes = bytes.load(Ordering::Relaxed);

    println!(
        "n={n} p50={:.3} p90={:.3} p99={:.3} p999={:.3} mean={:.3} stddev={:.3} max={:.3} rps={:.0} bytes={} errors={}",
        pct(50.0), pct(90.0), pct(99.0), pct(99.9), mean, stddev, max, rps, total_bytes,
        errors.load(Ordering::Relaxed)
    );
}
