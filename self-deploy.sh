#!/bin/bash
# Self-contained deploy — no sudo needed, runs as current user
set -e

echo "[DEPLOY] Installing Project Q coordinator..."

# Binary dir
mkdir -p ~/.local/bin
cp /tmp/project-qd ~/.local/bin/project-qd
chmod +x ~/.local/bin/project-qd

# Service dir
mkdir -p ~/.config/systemd/user
cat > ~/.config/systemd/user/project-qd-coordinator.service << 'EOF'
[Unit]
Description=Project Q Daemon Coordinator
After=network.target

[Service]
ExecStart=%h/.local/bin/project-qd --coordinator --tcp-port 9100 --udp-port 9101
Restart=always
RestartSec=5

[Install]
WantedBy=default.target
EOF

# Enable linger (so user service survives logout)
# This DOES need sudo, but we try it; if it fails, user can do it manually
if command -v loginctl >/dev/null 2>&1; then
    loginctl enable-linger "$USER" 2>/dev/null || echo "[DEPLOY] Note: loginctl failed (may need sudo for linger)"
fi

# Start
systemctl --user daemon-reload
systemctl --user enable project-qd-coordinator.service
systemctl --user restart project-qd-coordinator.service

sleep 2
echo ""
echo "[DEPLOY] Status:"
systemctl --user status project-qd-coordinator.service --no-pager || true
echo ""
echo "[DEPLOY] Listening ports:"
ss -tlnp 2>/dev/null | grep 9100 || echo "  TCP 9100 not detected"
ss -ulnp 2>/dev/null | grep 9101 || echo "  UDP 9101 not detected"
echo ""
echo "[DEPLOY] Done."
