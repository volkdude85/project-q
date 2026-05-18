#!/bin/bash
# Self-contained worker deploy — no sudo needed, runs as current user
set -e

COORD="${1:-192.168.0.145}"
TCP="${2:-9100}"
UDP="${3:-9101}"
NODE="${4:-$(hostname)}"

echo "[DEPLOY] Installing Project Q worker pointing to $COORD:$TCP..."

mkdir -p ~/.local/bin
cp /tmp/project-qd ~/.local/bin/project-qd
chmod +x ~/.local/bin/project-qd

mkdir -p ~/.config/systemd/user
cat > ~/.config/systemd/user/project-qd-worker.service << EOF
[Unit]
Description=Project Q Daemon Worker
After=network.target

[Service]
ExecStart=%h/.local/bin/project-qd --worker --connect $COORD:$TCP --name $NODE
Restart=always
RestartSec=5

[Install]
WantedBy=default.target
EOF

if command -v loginctl >/dev/null 2>&1; then
    loginctl enable-linger "$USER" 2>/dev/null || echo "[DEPLOY] Note: loginctl failed (may need sudo for linger)"
fi

systemctl --user daemon-reload
systemctl --user enable project-qd-worker.service
systemctl --user restart project-qd-worker.service

sleep 2
echo ""
echo "[DEPLOY] Status:"
systemctl --user status project-qd-worker.service --no-pager || true
echo ""
echo "[DEPLOY] Done."
