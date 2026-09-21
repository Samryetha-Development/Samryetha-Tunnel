#!/bin/bash
# 协议基准：直连基线 / JSON 隧道 / 私有二进制隧道 对比
# 输出：延迟分位、抖动、吞吐、协议字节数
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SERVER_BIN="$ROOT/tunnel-server/target/release/tunnel-server"
[ -x "$SERVER_BIN" ] || SERVER_BIN="$ROOT/tunnel-server/target/debug/tunnel-server"
LITE="$ROOT/tunnel-lite/build/tunnel-lite"
LOADGEN="$ROOT/tunnel-server/target/release/loadgen"
[ -x "$LOADGEN" ] || LOADGEN="$ROOT/tunnel-server/target/debug/loadgen"

PORT=18090
LOCAL=18081
CONC="${CONC:-16}"
TOTAL="${TOTAL:-800}"
USER="bench@test.dev"
SLUG="bench"

echo "=== tunnel protocol benchmark (conc=$CONC total=$TOTAL) ==="

# 本地被测服务（返回固定 21 字节）
python3 - "$LOCAL" <<'PY' &
import sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
port=int(sys.argv[1])
class H(BaseHTTPRequestHandler):
    protocol_version="HTTP/1.1"
    def do_GET(self):
        b=b"hello from cpp client"
        self.send_response(200); self.send_header("Content-Type","text/plain")
        self.send_header("Content-Length",str(len(b))); self.end_headers(); self.wfile.write(b)
    def log_message(self,*a): pass
class Srv(ThreadingHTTPServer):
    request_queue_size = 256
    daemon_threads = True
Srv(("127.0.0.1",port),H).serve_forever()
PY
LOCAL_PID=$!

# 服务端（放开限流，便于压测）
RATE_PER_SEC=200000 RATE_BURST=200000 AUTH_MODE=dev ROLE=server BIND=127.0.0.1 \
  PORT=$PORT BASE_DOMAIN=frp.test DATABASE_URL="sqlite:///tmp/tunnel-bench.db?mode=rwc" \
  COOKIE_SECURE=false "$SERVER_BIN" >/tmp/bench-server.log 2>&1 &
SRV_PID=$!
sleep 1.5

cleanup() { kill $LOCAL_PID $SRV_PID 2>/dev/null; pkill -f "client-id bench-" 2>/dev/null; }
trap cleanup EXIT

TOK=$(curl -s -X POST "http://127.0.0.1:$PORT/auth/dev-token" -H 'content-type: application/json' \
      -d "{\"email\":\"$USER\"}" | python3 -c 'import sys,json;print(json.load(sys.stdin)["token"])')

echo
printf '%-22s %s\n' "direct-local" "$($LOADGEN 127.0.0.1 $LOCAL / $CONC $TOTAL)"
echo

for P in json binary; do
  LOG="/tmp/bench-$P.log"
  "$LITE" --server "ws://127.0.0.1:$PORT/tunnel" --token "$TOK" \
      --client-id "bench-$P" --proto "$P" \
      --tunnel "id=bench,path=/bench,local=127.0.0.1:$LOCAL" >"$LOG" 2>&1 &
  CPID=$!
  sleep 1.5
  RES=$($LOADGEN 127.0.0.1 $PORT /$SLUG/bench $CONC $TOTAL frp.test)
  kill -INT $CPID 2>/dev/null; wait $CPID 2>/dev/null
  BYTES=$(grep -o '累计发送 [^）]*' "$LOG" | tail -n1)
  printf '%-22s %s\n' "tunnel-$P" "$RES"
  printf '%-22s %s\n' "" "$BYTES"
done

echo
echo "（p50/p90/p99/p999/mean/stddev/max 单位 ms，越小越好；stddev 越小抖动越低）"
