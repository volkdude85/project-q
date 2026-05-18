# Project Q — Distributed Compile Farm Daemon
## C++ UDP mesh, persistent queue, auto-discovery

## Current State
- Daemon compiles: `project-qd` binary at `src/daemon/build/project-qd`
- TCP control + UDP heartbeat protocol wired
- SQLite queue sync working
- Porsche online, Ally & Laptop down

## Order of Operations

### Phase 1 — Coordinator Deployment (NOW)
- [x] Daemon builds clean
- [ ] Install binary to `/usr/local/bin/project-qd` on Porsche
- [ ] Install systemd user service `project-qd-coordinator.service`
- [ ] Enable + start coordinator on Porsche (TCP 9100, UDP 9101)
- [ ] Verify: `ss -tlnp | grep 9100`, UDP heartbeat reception
- [ ] Add firewall rule if needed (`iptables -I INPUT -p udp --dport 9101 -j ACCEPT`)

### Phase 2 — Worker Deployment
- [ ] Copy binary to Ally `~/.local/bin/project-qd`
- [ ] Copy binary to Laptop `~/.local/bin/project-qd`
- [ ] Install systemd user service `project-qd@.service` on each node
- [ ] Start workers pointing at Porsche:9100
- [ ] Verify heartbeat reception in coordinator logs
- [ ] Test task dispatch end-to-end

### Phase 3 — Queue Integration
- [ ] Migrate `~/.local/share/project-q/queue.db` schema to match daemon
- [ ] Seed pending tasks from `COMPILE_QUEUE.md`
- [ ] Wire q-master script to talk to daemon via TCP instead of SSH
- [ ] Artifact sync: replace rsync with daemon artifact push/pull

### Phase 4 — UI / CLI
- [ ] `qcli status` shows node mesh + load
- [ ] `qcli submit <task>` injects into queue
- [ ] `qcli logs <task_id>` fetches from node that ran it
- [ ] Real-time curses TUI (optional, later)

### Phase 5 — Hardening
- [ ] Task sandboxing (bubblewrap / firejail)
- [ ] Artifact checksums
- [ ] Node authentication (HMAC on heartbeat)
- [ ] Retry with exponential backoff on worker reconnect
