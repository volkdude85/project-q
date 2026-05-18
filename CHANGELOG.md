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

## 2026-05-18 — Phase 4: PQD-007 Artifact Sync Over TCP

**Model:** deepseek-v4-flash  
**Provider:** ollama-cloud  

### Added
- `src/daemon/artifacts.{h,cpp}` — Base64 encode/decode (no external deps), file scanner for output artifacts. Gated on `outputs` field in queue task.

### Modified
- `protocol.{h,cpp}` — Added `makeArtifactSync`, `makeArtifactData`, `makeArtifactAck` message builders for artifact transfer over TCP
- `worker.cpp` — After task result, reads `outputs` field from SQLite, finds files in workdir, base64-encodes and sends as `artifact_data` frames
- `coordinator.cpp` — Handles `artifact_data` frames, base64-decodes, writes to `~/.local/share/project-q/cache/artifacts/`. Sends `artifact_ack` on success.
- `queue_sync.{h,cpp}` — Added `getTaskOutputs()` to read a task's `outputs` column from SQLite
- `CMakeLists.txt` — Added `artifacts.cpp` to the build

### Build Result
- Clean compile, zero warnings (GCC 16.1.1)
- **End-to-end verified**: task dispatched → executed → result reported → artifact base64'd over TCP → decoded on coordinator → file contents matched

### Known Items
- Farm SSH still broken on all nodes (Porsche connection reset, Laptop no key, Ally offline)
- Dad joke names pending: "Project Queue'd", "farm-fist", "q-demon", "UDP speed demon"
- Task 008b added to palace: private GitHub repo for cross-machine access
2026-05-18 15:20:00 UTC | Garuda | deploy-worker.sh, project-qd | Fixed deploy script on ROG Ally USB to use local binaries instead of GitHub clone. Caught outdated IP references pointing to .145 and .78 instead of current Block IP (.75). Coordinator now running on Block (192.168.0.75:9100/tcp, 9101/udp). ROG Ally worker deploy script corrected to connect to 192.168.0.75:9100. Note: q-worker and q-master still have hardcoded 192.168.0.145 references for artifact/rsync operations.
