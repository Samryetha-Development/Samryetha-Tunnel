#!/bin/bash
# 队头阻塞（HOL）对照实验：
#   一条隧道持续大流量下载，同时探测另一条隧道的小请求延迟
#   shared  = 两条隧道共用一条连接
#   isolate = 两条隧道各自独立连接
# 预期：shared 下 fast 的延迟会被 bulk 拖高；isolate 下几乎不受影响
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SERVER_BIN="$ROOT/tunnel-server/target/release/tunnel-server"
[ -x "$SERVER_BIN" ] || SERVER_BIN="$ROOT/tunnel-server/target/debug/tunnel-server"
LITE="$ROOT/tunnel-lite/build/tunnel-lite"
LOADGEN="$ROOT/tunnel-server/target/release/loadgen"
[ -x "$LOADGEN" ] || LOADGEN="$ROOT/tunnel-server/target/debug/loadgen"

PORT=18090
FAST=18081
BULK=18082
USER="hol@test.dev"
SLUG="hol"
CONC="${CONC:-1}"
TOTAL="${TOTAL:-60}"
BULK_MB="${BULK_MB:-8}"
BULK_N="${BULK_N:-2}"

echo "=== HOL 对照：${BULK_MB}MB 持续下载 ×${BULK_N}，探测小请求延迟 (conc=$CONC total=$TOTAL) ==="

python3 - "$FAST" "$BULK" "$BULK_MB" <<'PY' &
import sys, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
fast=int(sys.argv[1]); bulk=int(sys.argv[2]); mb=int(sys.argv[3])
class FH(BaseHTTPRequestHandler):
    protocol_version="HTTP/1.1"
    def do_GET(self):
        b=b"ok"; self.send_response(200); self.send_header("Content-Length",str(len(b))); self.end_headers(); self.wfile.write(b)
    def log_message(self,*a): pass
class BH(BaseHTTPRequestHandler):
    # 无限流：不设 Content-Length，持续占满共享连接（close-delimited）
    protocol_version="HTTP/1.1"
    def do_GET(self):
        self.send_response(200); self.send_header("Content-Type","application/octet-stream"); self.end_headers()
        chunk=b"x"*65536
        try:
            while True:
                self.wfile.write(chunk); self.wfile.flush(); time.sleep(0.001)
        except Exception: pass
    def log_message(self,*a): pass
class S(ThreadingHTTPServer): request_queue_size=256; daemon_threads=True
import threading
threading.Thread(target=lambda: S(("127.0.0.1",fast),FH).serve_forever(), daemon=True).start()
S(("127.0.0.1",bulk),BH).serve_forever()
PY
LOCAL_PID=$!

RATE_PER_SEC=200000 RATE_BURST=200000 AUTH_MODE=dev ROLE=server BIND=127.0.0.1 \
  PORT=$PORT BASE_DOMAIN=frp.test DATABASE_URL="sqlite:///tmp/tunnel-hol.db?mode=rwc" \
  COOKIE_SECURE=false "$SERVER_BIN" >/tmp/hol-server.log 2>&1 &
SRV_PID=$!
sleep 1.5

cleanup() { kill $LOCAL_PID $SRV_PID 2>/dev/null; pkill -f "client-id hol-" 2>/dev/null; pkill -f "/$SLUG/bulk" 2>/dev/null; }
trap cleanup EXIT

TOK=$(curl -s -X POST "http://127.0.0.1:$PORT/auth/dev-token" -H 'content-type: application/json' \
      -d "{\"email\":\"$USER\"}" | python3 -c 'import sys,json;print(json.load(sys.stdin)["token"])')

for MODE in shared isolate; do
  FLAG=""; [ "$MODE" = "isolate" ] && FLAG="--isolate"
  $LITE --server "ws://127.0.0.1:$PORT/tunnel" --token "$TOK" \
      --client-id "hol-$MODE" --proto binary $FLAG \
      --tunnel "id=fast,path=/fast,local=127.0.0.1:$FAST" \
      --tunnel "id=bulk,path=/bulk,local=127.0.0.1:$BULK" >"/tmp/hol-$MODE.log" 2>&1 &
  CPID=$!
  sleep 1.5

  # 空载基线
  IDLE=$($LOADGEN 127.0.0.1 $PORT /$SLUG/fast $CONC $TOTAL frp.test | awk '{print $2, $3, $4}')

  # 持续下载制造压力
  BPIDS=""
  for i in $(seq 1 $BULK_N); do
    curl -s --max-time 30 -H "Host: frp.test" "http://127.0.0.1:$PORT/$SLUG/bulk" -o /dev/null &
    BPIDS="$BPIDS $!"
  done
  sleep 0.8
  LOADED=$($LOADGEN 127.0.0.1 $PORT /$SLUG/fast $CONC $TOTAL frp.test | awk '{print $2, $3, $4}')
  kill $BPIDS 2>/dev/null; wait $BPIDS 2>/dev/null

  kill -INT $CPID 2>/dev/null; wait $CPID 2>/dev/null
  printf '%-10s 空载: %s   大流量下: %s\n' "$MODE" "$IDLE" "$LOADED"
done

echo
echo "（字段：p50 p90 p99，单位 ms；对比同一模式下空载 vs 大流量，以及 shared vs isolate）"
