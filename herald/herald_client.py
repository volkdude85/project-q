#!/usr/bin/env python3
"""
herald_client.py — Python client for the Herald farm coordinator.

Connects to the coordinator's TCP control port (9100 default) and uses the
length-prefixed JSON protocol to submit tasks, query nodes, and check results.

Usage:
    from herald_client import HeraldClient
    hc = HeraldClient("127.0.0.1", 9100)
    print(hc.get_farm_status())
    task_id = hc.send_task("echo hello world")
    print(hc.wait_for_task(task_id, poll_sec=2, timeout_sec=60))
"""

import json
import socket
import struct
import time


def _encode_frame(obj: dict) -> bytes:
    """Length-prefixed JSON frame."""
    payload = json.dumps(obj, separators=(",", ":")).encode()
    return struct.pack("!I", len(payload)) + payload


def _decode_frame(data: bytes) -> dict:
    """Strip 4-byte length prefix and parse JSON."""
    return json.loads(data[4:].decode())


class HeraldClient:
    """Low-level TCP client for the Herald coordinator."""

    def __init__(self, host: str = "127.0.0.1", port: int = 9100, connect_timeout: float = 5.0):
        self.host = host
        self.port = port
        self._connect_timeout = connect_timeout
        self._sock: socket.socket | None = None

    # ── connection management ──────────────────────────────────────

    def connect(self):
        """Open TCP connection to coordinator. Always makes a fresh connection."""
        self.close()
        self._sock = socket.create_connection(
            (self.host, self.port), timeout=self._connect_timeout
        )

    def reconnect(self):
        """Force close and reconnect."""
        self.close()
        self._sock = socket.create_connection(
            (self.host, self.port), timeout=self._connect_timeout
        )

    def close(self):
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *args):
        self.close()

    # ── raw send / recv ────────────────────────────────────────────

    def _send(self, msg: dict) -> dict:
        """Send a command frame and read one response frame."""
        self.connect()
        self._sock.sendall(_encode_frame(msg))
        hdr = self._sock.recv(4)
        if len(hdr) < 4:
            raise ConnectionError("Coordinator closed connection")
        payload_len = struct.unpack("!I", hdr)[0]
        payload = self._sock.recv(payload_len)
        while len(payload) < payload_len:
            chunk = self._sock.recv(payload_len - len(payload))
            if not chunk:
                raise ConnectionError("Coordinator closed connection mid-frame")
            payload += chunk
        return json.loads(payload.decode())

    def _send_raw(self, msg: dict) -> bytes:
        """Send command frame and return raw response bytes (for streaming)."""
        self.connect()
        self._sock.sendall(_encode_frame(msg))
        hdr = self._sock.recv(4)
        if len(hdr) < 4:
            raise ConnectionError("Coordinator closed connection")
        payload_len = struct.unpack("!I", hdr)[0]
        payload = self._sock.recv(payload_len)
        while len(payload) < payload_len:
            chunk = self._sock.recv(payload_len - len(payload))
            if not chunk:
                raise ConnectionError("Coordinator closed connection mid-frame")
            payload += chunk
        return payload

    # ── public commands ────────────────────────────────────────────

    def ping(self) -> dict:
        """Ping the coordinator. Returns pong response."""
        return self._send({"cmd": "ping"})

    def list_nodes(self) -> list[dict]:
        """Query all registered worker nodes."""
        resp = self._send({"cmd": "list_nodes"})
        return resp.get("nodes", [])

    def get_farm_status(self) -> dict:
        """
        Return a summary dict with:
          - nodes: list of node dicts
          - count: int
          - node_stats: per-node name->{load, active_tasks, last_heartbeat_sec_ago, fully_registered}
        """
        nodes = self.list_nodes()
        stats = {}
        for n in nodes:
            stats[n["name"]] = {
                "load": n.get("load", 0),
                "active_tasks": n.get("active_tasks", 0),
                "last_heartbeat_sec_ago": n.get("last_heartbeat_sec_ago", -1),
                "fully_registered": n.get("fully_registered", False),
                "address": n.get("address", ""),
            }
        return {"count": len(nodes), "nodes": nodes, "node_stats": stats}

    def submit_task(
        self, name: str, command: str, timeout_sec: int = 1800, target_node: str = ""
    ) -> dict:
        """
        Submit a task to the farm.

        If target_node is set, the coordinator routes directly to that node
        instead of round-robin.

        Returns: {"cmd": "submit_ack", "task_id": <int>}
        """
        msg = {"cmd": "submit", "name": name, "command": command, "timeout_sec": timeout_sec}
        if target_node:
            msg["target_node"] = target_node
        return self._send(msg)

    def send_task(self, command: str, name: str | None = None, timeout_sec: int = 1800, target_node: str = "") -> int:
        """
        Submit a task and return the task ID.

        If target_node is set, the coordinator routes directly to that node.
        """
        if name is None:
            name = command[:60]
        resp = self.submit_task(name, command, timeout_sec, target_node)
        return resp.get("task_id", -1)

    def get_task_status(self, task_id: int) -> dict:
        """Query a task by ID. Returns full task record or {'found': False}."""
        return self._send({"cmd": "task_query", "task_id": task_id})

    def wait_for_task(
        self,
        task_id: int,
        poll_sec: float = 2.0,
        timeout_sec: float = 1800.0,
    ) -> dict:
        """
        Poll a task until it reaches a terminal status (done/failed) or times out.

        Returns the final task record dict.
        """
        deadline = time.monotonic() + timeout_sec
        while time.monotonic() < deadline:
            record = self.get_task_status(task_id)
            if not record.get("found", False):
                time.sleep(poll_sec)
                continue
            status = record.get("status", "unknown")
            if status in ("done", "failed"):
                return record
            time.sleep(poll_sec)
        return {"found": True, "task_id": task_id, "status": "timeout", "error_log": f"Poll timed out after {timeout_sec}s"}

    # ── multi-task batch helpers ───────────────────────────────────

    def send_tasks(self, tasks: list[dict]) -> list[int]:
        """
        Submit multiple tasks in sequence.

        Each task dict: {"command": str, "name"?: str, "timeout_sec"?: int}
        Returns list of task IDs.
        """
        ids = []
        for t in tasks:
            ids.append(
                self.send_task(
                    command=t["command"],
                    name=t.get("name"),
                    timeout_sec=t.get("timeout_sec", 1800),
                )
            )
        return ids

    def wait_for_tasks(
        self, task_ids: list[int], poll_sec: float = 2.0, timeout_sec: float = 1800.0
    ) -> list[dict]:
        """
        Wait for all given task IDs to finish. Returns list of final records.
        """
        results = []
        deadline = time.monotonic() + timeout_sec
        pending = set(task_ids)
        while pending and time.monotonic() < deadline:
            done = set()
            for tid in pending:
                record = self.get_task_status(tid)
                if record.get("status") in ("done", "failed"):
                    results.append(record)
                    done.add(tid)
            pending -= done
            if pending:
                time.sleep(poll_sec)
        # Any remaining pending = timeout
        for tid in pending:
            results.append({
                "found": True,
                "task_id": tid,
                "status": "timeout",
                "error_log": f"Batch poll timed out after {timeout_sec}s",
            })
        return results


# ── command-line usage ────────────────────────────────────────────────

if __name__ == "__main__":
    import sys

    def _usage():
        print("Usage:")
        print("  herald_client.py list-nodes")
        print("  herald_client.py submit <command> [name] [timeout_sec]")
        print("  herald_client.py status <task_id>")
        print("  herald_client.py wait <task_id> [poll_sec] [timeout_sec]")
        sys.exit(1)

    args = sys.argv[1:]
    if not args:
        _usage()

    cmd = args[0]
    host = "127.0.0.1"
    port = 9100

    # Allow HOST override via env
    import os
    host = os.environ.get("HERALD_HOST", host)
    port = int(os.environ.get("HERALD_PORT", port))

    with HeraldClient(host, port) as hc:
        if cmd == "list-nodes":
            nodes = hc.list_nodes()
            print(json.dumps(nodes, indent=2))
        elif cmd == "submit":
            command = args[1] if len(args) > 1 else _usage()
            name = args[2] if len(args) > 2 else command[:60]
            timeout = int(args[3]) if len(args) > 3 else 1800
            tid = hc.send_task(command, name, timeout)
            print(f"Submitted task #{tid}")
        elif cmd == "status":
            tid = int(args[1])
            print(json.dumps(hc.get_task_status(tid), indent=2))
        elif cmd == "wait":
            tid = int(args[1])
            poll = float(args[2]) if len(args) > 2 else 2.0
            to = float(args[3]) if len(args) > 3 else 1800.0
            print(json.dumps(hc.wait_for_task(tid, poll, to), indent=2))
        else:
            _usage()
