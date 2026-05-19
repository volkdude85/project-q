#!/bin/bash
set -e

COORDINATOR_HOST="192.168.0.75"
COORDINATOR_PORT="9100"
BUILD_DIR="/home/volkdude/Projects/project-q/build"
BINARY="$BUILD_DIR/project-qd"

NODES=(
  "porchebox@192.168.0.145:porchebox-b450aorusm"
  "seanvolk@192.168.0.78:seanvolk-archlaptop"
  "volkdude85@192.168.0.233:volkdude85-rogallyrc71lrc71l"
)

echo "=== Herald Deploy ==="
echo "Coordinator: $COORDINATOR_HOST:$COORDINATOR_PORT"

# Step 1: Rebuild
echo ""
echo "[1/3] Building fresh binary..."
cd "$BUILD_DIR"
make -j$(nproc) 2>&1 | tail -1
echo "       Binary size: $(stat --format=%s "$BINARY") bytes"

# Step 2: Push and restart on each node
echo ""
echo "[2/3] Deploying to nodes..."
for NODE_SPEC in "${NODES[@]}"; do
  USER_HOST="${NODE_SPEC%%:*}"
  NODE_NAME="${NODE_SPEC##*:}"
  REMOTE_BIN="/home/${USER_HOST%%@*}/.local/bin/project-qd"
  PIDFILE="/tmp/project-qd-${NODE_NAME}.pid"

  echo "  -> $NODE_NAME ($USER_HOST)"

  # Push binary (delete first to avoid scp failure from busy file)
  ssh -o StrictHostKeyChecking=accept-new "$USER_HOST" "rm -f $REMOTE_BIN" 2>/dev/null
  scp -o StrictHostKeyChecking=accept-new -q "$BINARY" "${USER_HOST}:${REMOTE_BIN}"
  ssh -o StrictHostKeyChecking=accept-new "$USER_HOST" "chmod +x $REMOTE_BIN"
  echo "     Binary pushed + chmod"

  # Kill old process via PID file guard, then pkill as backup
  ssh -o StrictHostKeyChecking=accept-new "$USER_HOST" "
    if [ -f '$PIDFILE' ]; then
      OLPID=\$(cat '$PIDFILE')
      kill \$OLPID 2>/dev/null && sleep 1
      kill -0 \$OLPID 2>/dev/null && kill -9 \$OLPID 2>/dev/null
    fi
    pkill -x project-qd 2>/dev/null || true
    sleep 1
  " 2>/dev/null

  # Start fresh worker
  ssh -o StrictHostKeyChecking=accept-new "$USER_HOST" "
    nohup $REMOTE_BIN --worker \
      --connect ${COORDINATOR_HOST}:${COORDINATOR_PORT} \
      --node-name ${NODE_NAME} \
      > /dev/null 2>&1 &
    echo \$! > '$PIDFILE'
    echo '     Worker started, PID: '\$!
  " 2>&1
done

# Step 3: Verify
echo ""
echo "[3/3] Verifying connections..."
sleep 8

python3 -c "
import socket, struct, json, time

def send(msg):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect(('127.0.0.1', $COORDINATOR_PORT))
    p = json.dumps(msg).encode()
    s.sendall(struct.pack('!I', len(p)) + p)
    h = s.recv(4)
    pl = struct.unpack('!I', h)[0]
    r = json.loads(s.recv(pl))
    s.close()
    return r

# List nodes
r = send({'cmd':'list_nodes'})
print(f'Registered nodes: {r.get(\"count\",0)}')
for n in r.get('nodes',[]):
    print(f'  {n[\"name\"]:40s} load={n[\"load\"]:5d} tasks={n[\"active_tasks\"]} '
          f'hb_ago={n[\"last_heartbeat_sec_ago\"]:3d}s registered={n[\"registered_sec_ago\"]:3d}s ago '
          f'registered={n[\"fully_registered\"]}')

# Submit a task per node
for name in ['porchebox-b450aorusm', 'seanvolk-archlaptop', 'volkdude85-rogallyrc71lrc71l']:
    r = send({'cmd':'submit','name':f'verify-{name}','command':'hostname && uptime -p','timeout_sec':20})
    tid = r['task_id']
    time.sleep(7)
    r2 = send({'cmd':'task_query','task_id':tid})
    n = r2.get('assigned_node','?')
    s = r2.get('status','?')
    o = r2.get('stdout','').strip().replace(chr(10),' | ')
    print(f'  task#{tid:3d} name={name:40s} status={s:8s} node={n:40s} out={o[:60]}')
"
