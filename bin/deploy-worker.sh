#!/bin/bash
set -e
REPO_URL="https://github.com/volkdude85/compile-farm"
INSTALL_DIR="${HOME}/.local/bin"
SHARE_DIR="${HOME}/.local/share/project-q"
mkdir -p "$INSTALL_DIR" "$SHARE_DIR/cache/artifacts"

TMPDIR=$(mktemp -d)
git clone --depth 1 "$REPO_URL" "$TMPDIR/repo"

if [ -f "$TMPDIR/repo/bin/project-qd" ]; then
    cp "$TMPDIR/repo/bin/project-qd" "$INSTALL_DIR/project-qd"
elif [ -f "$TMPDIR/repo/src/daemon/build/project-qd" ]; then
    cp "$TMPDIR/repo/src/daemon/build/project-qd" "$INSTALL_DIR/project-qd"
else
    echo "[!] binary not found in repo"
    exit 1
fi

chmod +x "$INSTALL_DIR/project-qd"
rm -rf "$TMPDIR"

COORDINATOR="${1:-192.168.0.78}"
PORT="${2:-9100}"

if command -v systemctl >/dev/null 2>&1; then
    mkdir -p "${HOME}/.config/systemd/user"
    cat > "${HOME}/.config/systemd/user/speed-daemon-Q-worker.service" <<SVC
[Unit]
Description=Speed Daemon Q Worker
After=network.target

[Service]
ExecStart=%h/.local/bin/project-qd --worker --connect ${COORDINATOR}:${PORT} --name %H
Restart=always
RestartSec=5

[Install]
WantedBy=default.target
SVC
    systemctl --user daemon-reload
    systemctl --user enable speed-daemon-Q-worker.service
    systemctl --user start speed-daemon-Q-worker.service
    echo "[+] systemd worker started"
elif command -v rc-update >/dev/null 2>&1; then
    sudo mkdir -p /etc/local.d
    cat > /tmp/speed-daemon-Q-worker.start <<RC
#!/bin/bash
start-stop-daemon --start --background --make-pidfile --pidfile /run/speed-daemon-Q-worker.pid --exec "${HOME}/.local/bin/project-qd" -- --worker --connect ${COORDINATOR}:${PORT} --name "$(hostname)"
RC
    sudo mv /tmp/speed-daemon-Q-worker.start /etc/local.d/
    sudo chmod +x /etc/local.d/speed-daemon-Q-worker.start
    sudo rc-update add local default 2>/dev/null || true
    sudo /etc/local.d/speed-daemon-Q-worker.start
    echo "[+] OpenRC worker started"
else
    nohup "${INSTALL_DIR}/project-qd" --worker --connect "${COORDINATOR}:${PORT}" --name "$(hostname)" > "${SHARE_DIR}/worker.log" 2>&1 &
    echo "[+] manual worker started (pid $!)"
fi

echo "[*] Deployed. Coordinator: $COORDINATOR:$PORT"
