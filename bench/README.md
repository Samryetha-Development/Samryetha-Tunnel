# 基准测试：JSON 兼容协议 vs 私有二进制协议（v3）

## 一句话结论

私有二进制协议（v3）**协议字节数稳定 −54%**（可复现的确定性收益）；
但在本机回环环境下，**延迟分位差异落在轮次噪声内**，不显著。要拿到延迟/抖动收益，必须换掉瓶颈所在的连接层（见文末）。

## 线格式（v3）

每一帧 = 一个 WebSocket **二进制**消息，字段手工编码，无 JSON、无 base64：

```
varint : LEB128 无符号
str    : varint 长度 + UTF-8
bytes  : varint 长度 + 原始字节
u8/u16 : 小端定长

Register(0x01)   : client_id, n, n×{id, proto(u8), sub, path, local}
RegisterAck(0x02): ok(u8), error, n, n×{id, proto, url, port(u16)}
Ping(0x03) / Pong(0x04)
OpenStream(0x05) : stream_id, tunnel_id, proto, method, path, headers, body
ResponseHead(0x06): stream_id, status(u16), headers
Chunk(0x07)      : stream_id, bytes      （双向）
End(0x08) / Abort(0x09) / CloseStream(0x0A): stream_id
```

协商：客户端连 `/tunnel?v=3` 走二进制，否则走 JSON（完全向后兼容，`wscat` 可直接调 JSON 模式）。
实现：Rust `tunnel-server/src/binproto.rs`，C++ `tunnel-lite/src/codec.hpp`。

## 复现

```bash
cd tunnel-server && cargo build --release
cd tunnel-lite  && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
CONC=16 TOTAL=800  bash bench/run-bench.sh
CONC=64 TOTAL=1200 bash bench/run-bench.sh   # 建议连跑 3 次看方差
```

链路：`loadgen → 服务端 → WS 控制通道 → 客户端 → 本地服务`（本机回环，无丢包无 RTT，绝对值偏乐观）。
本地被测服务固定返回 21 字节；`direct-local` 为不经隧道的直连基线。

## 结果

### 协议字节数（确定性收益）

并发 16、800 请求：

| 协议 | 发送 | 接收 | 合计 |
|---|---|---|---|
| tunnel-json | 229.5 KB | 151.6 KB | **381.1 KB** |
| tunnel-binary | 114.1 KB | 61.0 KB | **175.1 KB** |

并发 64、1600 请求：

| 协议 | 发送 | 接收 | 合计 |
|---|---|---|---|
| tunnel-json | 461.0 KB | 303.8 KB | **764.8 KB** |
| tunnel-binary | 228.2 KB | 121.9 KB | **350.1 KB** |

→ **−54.1% / −54.2%**，与并发无关，稳定复现。原因很直接：base64 的 +33% 膨胀和 JSON 键名都没了。

### 延迟分位（噪声内，不显著）

并发 16、800 请求（单位 ms）：

| 路径 | p50 | p90 | p99 | p999 | mean | stddev | max |
|---|---|---|---|---|---|---|---|
| direct-local | 1.583 | 2.091 | 2.687 | 3.218 | 1.660 | 0.345 | 3.304 |
| tunnel-json | 2.072 | 2.815 | 4.893 | 6.832 | 2.285 | 0.685 | 7.255 |
| tunnel-binary | 2.126 | 2.933 | 4.976 | 6.530 | 2.281 | 0.685 | 6.684 |

并发 64、1200 请求，**连跑 3 轮的 p999**：

| 轮次 | tunnel-json | tunnel-binary |
|---|---|---|
| run 1 | 17.94 | 17.55 |
| run 2 | 14.71 | 17.98 |
| run 3 | 19.01 | 17.98 |

→ 两者区间**重叠**，均约 8.0–8.4 ms（mean），**无法得出二进制显著更快的结论**。
（先前某一轮出现过 binary p99 12.4 vs json 14.3 的"优势"，复测证明那是一次噪声，已从结论中剔除。）

## 队头阻塞对照：每隧道独立连接（本架构下最有效的一招）

场景：`bulk` 隧道上挂 3 路**无限流**下载持续占满连接，同时用 `loadgen`（conc=1）探测 `fast` 隧道的小请求延迟。
`shared` = 两条隧道共用一条 WebSocket；`isolate` = 各自独立连接（`tunnel-lite --isolate`）。

复现：`BULK_N=3 CONC=1 TOTAL=100 bash bench/run-hol.sh`

大流量下 `fast` 的延迟（连跑 3 轮，单位 ms）：

| 轮次 | shared p50 / p90 / p99 | isolate p50 / p90 / p99 |
|---|---|---|
| run 1 | 0.354 / 0.651 / 0.955 | 0.306 / 0.356 / 0.441 |
| run 2 | 0.370 / 0.675 / 1.213 | 0.310 / 0.350 / 0.406 |
| run 3 | 0.344 / 0.956 / 1.175 | 0.304 / 0.346 / 0.448 |
| **中位** | **0.354 / 0.675 / 1.175** | **0.306 / 0.350 / 0.441** |

→ **p99 −62%、p90 −48%**，且 isolate 的 p90 与 p99 几乎重合（0.35 → 0.44），说明**抖动被压平**；
shared 则明显被 bulk 数据拖尾（p99 最高冲到 1.2ms，是 isolate 的 2.7 倍）。

原因：同一条 TCP/WebSocket 上，bulk 的大量数据帧与控制帧、其他隧道的响应帧**排队在同一条管道**里，
先到的 bulk 帧把后面的小响应顶在后面；拆成独立连接后，互不排队。

> 注：本机回环没有丢包，因此这里体现的是**应用层复用导致的队头阻塞**；
> 真实网络上一旦发生丢包重传，共享连接的阻塞会更严重（TCP 层 HOL）。

## 诚实解读

- **带宽是真收益**：−54%，与负载无关，可直接带来出网成本、移动网络流量、弱网拥塞的改善。
- **回环上延迟差异不显著**：本机没有丢包、没有 RTT、没有队列，编码的 CPU 差异根本不在关键路径上，测出来的只有 OS 调度噪声。
- **隧道本身的开销很小**：直连 p50 ~1.6ms，经隧道 ~2.1ms，即隧道路径只多 ~0.5ms。
- **真正的瓶颈是连接层**：所有隧道复用一条 TCP。一旦有丢包，重传会让**所有**隧道一起等（队头阻塞），这才是"延迟忽高忽低"的根源；回环无丢包，所以这里完全测不出来。
- 二进制协议的价值要在**真实网络 + CPU 受限**时才体现：更少字节 = 更少拥塞窗口占用、更少编码 CPU = 高负载下更稳。

## 下一步（按收益排序）

1. ~~**关键隧道独立连接**~~ **已完成**：`tunnel-lite --isolate` / `conn=组名`；服务端已改为「一连接一组隧道」，p99 −62%（见上）。
2. **本地服务 keep-alive 连接池**：省掉每请求的本地 TCP 握手。
3. **真实网络压测**：用 `tc netem`（Linux）注入 1–3% 丢包 / 50ms RTT，量化丢包场景下的差距（预计共享连接的劣化会远大于回环）。
4. **QUIC**：每流独立、无 HOL；需放行 UDP 443（当前只有 TCP，暂缓）。
