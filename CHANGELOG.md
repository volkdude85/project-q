# Project Q Changelog

## 2026-05-18 — Phase 4: C++ Daemon (project-qd) — Build & Test

**Model:** deepseek-v4-flash  
**Provider:** ollama-cloud  
**Files touched:** 14 files created, 3 files modified

### Created
- `src/daemon/CMakeLists.txt` — CMake 3.20 C++20 build with SQLite3
- `src/daemon/config.{h,cpp}` — CLI arg parser (--coordinator, --worker, --port, --connect, --name, --caps)
- `src/daemon/protocol.{h,cpp}` — 24-byte UDP heartbeat + length-prefixed JSON TCP frames
- `src/daemon/coordinator.{h,cpp}` — Epoll TCP/UDP listener, node registry, dispatch loop, heartbeat timeout
- `src/daemon/worker.{h,cpp}` — TCP connect, register, UDP heartbeat thread, task_assign handler
- `src/daemon/taskrunner.{h,cpp}` — Fork-exec with pipe capture, configurable timeout, kill escalation
- `src/daemon/queue_sync.{h,cpp}` — SQLite read/write to existing queue.db, WAL mode
- `src/daemon/main.cpp` — Entry point with coordinator/worker mode selection
- `src/daemon/project-qd-coordinator.service` — Systemd unit for coordinator
- `src/daemon/project-qd@.service` — Systemd unit for workers
- `pending/004-daemon-cpp-mesh.md` — Phase 4 plan doc

### Modified
- `queue_sync.cpp` — Added WAL mode + busy_timeout for concurrent DB access
- `worker.cpp` — Fixed TCP EAGAIN handling, fixed JSON cmd field parsing (was matching "cmd":"task_assign" instead of shell command)
- `coordinator.cpp` — Added scanPendingTasks() for ongoing dispatch, not just on-registration

### Build Result
- Clean compile, zero warnings
- Coordinator binds TCP + UDP, accepts connections
- Worker connects, registers, sends UDP heartbeats
- Coordinator dispatches pending tasks to idle nodes
- **Known issue**: Worker parses shell command from "cmd" field — fixed but needs rebuild to verify

### Next
- Rebuild and test full dispatch→execute→result cycle
- PQD-007: Artifact sync over TCP (replace rsync)
- PQD-008: Deploy via systemd on Block + Porsche
