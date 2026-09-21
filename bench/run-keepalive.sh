#!/bin/bash
# 本地服务 keep-alive 连接池的结构性收益：请求数 vs 实际新建连接数
# 对照组：本地服务用 HTTP/1.0（每请求关连接，等价于旧行为）
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SERVER_BIN="$ROOT/tunnel-server/target/release/tunnel-server"
[ -x "$SERVER_BIN" ] || SERVER_BIN="$ROOT/tunnel-server/target/debug/tunnel-server"
LITE="$ROOT/tunnel-lite/build/tunnel-lite"
LOADGEN="$ROOT/tunnel-server/target/release/loadgen"
[ -x "$LOADGEN" ] || LOADGEN="$ROOT/tunnel-server/target/debug/loadgen"

PORT=18090
LOCAL=18081
USER="ka@test.dev"
SLUG="ka"
N="${N:-600}"

RATE_PER_SEC=200000 AUTH_MODE=dev ROLE=server BIND=127.0.0.1 \
  PORT=$PORT BASE_DOMAIN=frp.test DATABASE_URL="sqlite:///tmp/tunnel-ka.db?mode=rwc" \
  COOKIE_SECURE=false "$SERVER_BIN" >/tmp/ka-server.log 2>&1 &
SRV_PID=$!
sleep 1.5
TOK=$(curl -s -X POST "http://127.0.0.1:$PORT/auth/dev-token" -H 'content-type: application/json' \
      -d "{\"email\":\"$USER\"}" | python3 -c 'import sys,json;print(json.load(sys.stdin)["token"])')

cleanup() { kill ${LOCAL_PID:-0} $SRV_PID 2>/dev/null; pkill -f "client-id ka-" 2>/dev/null; }
trap cleanup EXIT

run_case() {
  local NAME="$1" LABEL="$2" VERSION="$3"
  rm -f /tmp/ka-conns.txt
  python3 - "$LOCAL" "$VERSION" <<'PY' >/dev/null 2>&1 &
import sys, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
port=int(sys.argv[1]); ver=sys.argv[2]
conns=set()
class H(BaseHTTPRequestHandler):
    protocol_version=ver
    def do_GET(self):
        conns.add(self.client_address[1])
        open("/tmp/ka-conns.txt","w").write("%d\n" % len(conns))
        b=b"ok"; self.send_response(200); self.send_header("Content-Length",str(len(b))); self.end_headers(); self.wfile.write(b)
    def log_message(self,*a): pass
class S(ThreadingHTTPServer):
    request_queue_size=256; daemon_threads=True
S(("127.0.0.1",port),H).serve_forever()
PY
  LOCAL_PID=$!
  sleep 0.8

  "$LITE" --server "ws://127.0.0.1:$PORT/tunnel" --token "$TOK" \
      --client-id "ka-$NAME" --proto binary \
      --tunnel "id=ka,path=/ka,local=127.0.0.1:$LOCAL" >"/tmp/ka-$NAME.log" 2>&1 &
  CPID=$!
  sleep 1.5
  $LOADGEN 127.0.0.1 $PORT /$SLUG/ka 16 $N frp.test >/dev/null
  sleep 0.3
  local STATS; STATS=$(cat /tmp/ka-conns.txt 2>/dev/null || echo "0 0")
  local CONNS; CONNS=$(echo "$STATS" | awk '{print $1}')
  local REUSE; REUSE=$(grep -c "reuse" "/tmp/ka-$NAME.log" 2>/dev/null || echo 0)
  printf '%-16s 请求 %-5s  本地新建连接 %-5s  复用命中 %s\n' "$LABEL" "$N" "$CONNS" "$REUSE"
  kill $CPID $LOCAL_PID 2>/dev/null; wait $CPID $LOCAL_PID 2>/dev/null
}

echo "=== 本地连接复用对比（隧道请求 $N 次）==="
run_case no-keepalive "无 keep-alive" "HTTP/1.0"
run_case keepalive "keep-alive 池" "HTTP/1.1"
