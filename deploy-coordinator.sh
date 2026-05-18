#!/bin/bash
# deploy-coordinator.sh — run this on Porsche as porchebox
set -e

echo "[DEPLOY] Installing Project Q coordinator daemon..."

# 1. Install binary
sudo cp /tmp/project-qd /usr/local/bin/project-qd
sudo chmod +x /usr/local/bin/project-qd

# 2. Install service for porchebox
mkdir -p ~/.config/systemd/user/
cat > ~/.config/systemd/user/project-qd-coordinator.service << 'EOF'
[Unit]
Description=Project Q Daemon Coordinator
After=network.target

[Service]
ExecStart=/usr/local/bin/project-qd --coordinator --tcp-port 9100 --udp-port 9101
Restart=always
RestartSec=5

[Install]
WantedBy=default.target
EOF

# 3. Enable lingering if not already
sudo loginctl enable-linger porchebox 2>/dev/null || true

# 4. Reload and start
systemctl --user daemon-reload
systemctl --user enable project-qd-coordinator.service
systemctl --user restart project-qd-coordinator.service

# 5. Verify
sleep 2
echo "[DEPLOY] Status:"
systemctl --user status project-qd-coordinator.service --no-pager
echo ""
echo "[DEPLOY] Ports:"
ss -tlnp | grep 9100 || echo "  TCP 9100 not listening"
ss -ulnp | grep 9101 || echo "  UDP 9101 not listening"

echo ""
echo "[DEPLOY] Done. Coordinator should be live on TCP 9100 + UDP 9101."
