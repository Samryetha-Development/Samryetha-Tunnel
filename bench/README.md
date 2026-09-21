# 基准测试：JSON 兼容协议 vs 私有二进制协议（v3）

## 一句话结论

私有二进制协议（v3）把**协议字节数砍掉 54%**，高并发下尾延迟（p99/p999）再降 **12–13%**；但延迟的主要瓶颈不在编码，而在连接层（TCP 单连接多路复用 + 客户端并发模型）。

## 线格式（v3）

每一帧 = 一个 WebSocket **二进制**消息，字段手工编码，无 JSON、无 base64：

```
varint : LEB128 无符号
str    : varint 长度 + UTF-8
bytes  : varint 长度 + 原始字节
u8/u16 : 小端定长

Register(0x01)  : client_id, n, n×{id, proto(u8), sub, path, local}
RegisterAck(0x02): ok(u8), error, n, n×{id, proto, url, port(u16)}
Ping(0x03) / Pong(0x04)
OpenStream(0x05) : stream_id, tunnel_id, proto, method, path, headers, body
ResponseHead(0x06): stream_id, status(u16), headers
Chunk(0x07)      : stream_id, bytes      （双向）
End(0x08) / Abort(0x09) / CloseStream(0x0A): stream_id
```

协商：客户端连 `/tunnel?v=3` 走二进制，否则走 JSON（完全向后兼容）。
实现：Rust `tunnel-server/src/binproto.rs`，C++ `tunnel-lite/src/codec.hpp`。

## 复现

```bash
cd tunnel-server && cargo build --release
cd tunnel-lite && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
CONC=16 TOTAL=800 bash bench/run-bench.sh
CONC=64 TOTAL=1600 bash bench/run-bench.sh
```

被测链路：`loadgen → 服务端(443 等价) → WS 控制通道 → 客户端 → 本地服务`（本机回环，故绝对值偏乐观，关注相对差异）。
本地被测服务固定返回 21 字节；`direct-local` 为不经隧道的直连基线。

## 结果

### 并发 16，800 请求

| 路径 | p50 | p90 | p99 | p999 | mean | **stddev(抖动)** | max | rps |
|---|---|---|---|---|---|---|---|---|
| direct-local | 1.583 | 2.091 | 2.687 | 3.218 | 1.660 | 0.345 | 3.304 | 9499 |
| tunnel-json | 2.072 | 2.815 | 4.893 | 6.832 | 2.285 | 0.685 | 7.255 | 6913 |
| **tunnel-binary** | 2.126 | 2.933 | 4.976 | 6.530 | 2.281 | 0.685 | 6.684 | 6928 |

单位 ms。

| 路径 | 发送 | 接收 | 合计 |
|---|---|---|---|
| tunnel-json | 229.5 KB | 151.6 KB | **381.1 KB** |
| tunnel-binary | 114.1 KB | 61.0 KB | **175.1 KB** |

→ 字节数 **−54.1%**；p50/p99 差异在噪声范围内。

### 并发 64，1600 请求

| 路径 | p50 | p90 | p99 | p999 | mean | stddev | max | rps |
|---|---|---|---|---|---|---|---|---|
| direct-local | 5.963 | 7.540 | 9.385 | 10.158 | 6.163 | 0.956 | 10.585 | 10148 |
| tunnel-json | 7.421 | 10.988 | 14.297 | 16.322 | 8.138 | 1.765 | 16.702 | 7724 |
| **tunnel-binary** | 7.292 | 11.023 | **12.420** | **14.345** | 8.051 | 1.708 | **14.721** | **7810** |

→ p99 **−13.1%**、p999 **−12.1%**、max **−11.9%**、rps **+1.1%**；字节数 **−54.2%**。

## 诚实解读

- **带宽是确定性收益**：无论并发多少，二进制稳定 −54%（base64 的 +33% 膨胀和 JSON 键名都没了）。
- **延迟收益集中在尾部、且需要高并发才显现**：p50 基本持平——单请求路径上编码不是瓶颈。
- **一个反直觉的发现**：优化客户端「每请求一个线程」为**线程池**之后，二进制在 conc=16 的尾延迟优势反而缩小了（此前因线程创建开销放大了 JSON 的 CPU 成本）。这说明**之前的差距有一部分来自 CPU 争用**，线程池把它消掉了。基线越干净，编码差异越只在真正 CPU 紧张时（高并发）体现。
- **真正的延迟瓶颈是连接层**：所有隧道复用一条 TCP，丢包重传会造成队头阻塞（HOL），这是抖动的主要来源；本次回环测试无丢包，所以看不到 HOL，也就看不到二进制之外的大幅提升。

## 下一步（按收益排序）

1. **关键隧道独立连接**：消除跨隧道 HOL，本地无需换传输即可砍掉大部分抖动。
2. **QUIC 传输**：每流独立、无 HOL，才是弱网低抖动的根本解。
3. **本地服务 keep-alive 连接池**：省掉每请求的本地 TCP 握手。
4. 把 p50/p95/p99/抖动做进 GUI 仪表，持续观测。
