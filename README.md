# Project-Q / Herald

Distributed compile farm daemon — LAN discovery, TCP task dispatch, zero SSH needed.

One machine runs the **coordinator**, any machine on the LAN can join as a **worker**. Workers appear automatically via UDP heartbeat, no config file needed.

## Quick start — worker

One paste, any Linux box:

```bash
curl -L https://github.com/volkdude85/project-q/releases/latest/download/project-qd \
  -o ~/.local/bin/project-qd && chmod +x ~/.local/bin/project-qd && \
  ~/.local/bin/project-qd --worker --connect 192.168.0.75:9100 --node-name $(hostname)
```

## Quick start — coordinator

```bash
~/project-qd --coordinator --node-name garuda
```

## Submit a task

```bash
python3 -c "
import socket, struct, json
s = socket.socket()
s.connect(('127.0.0.1', 9100))
msg = json.dumps({'cmd':'submit','name':'build','command':'make -j16','timeout_sec':1800}).encode()
s.sendall(struct.pack('!I',len(msg))+msg)
hlen = s.recv(4); plen = struct.unpack('!I',hlen)[0]
print(s.recv(plen))
"
```

## Build from source

```bash
cmake -B build
cmake --build build
```

Requires Qt6 (Widgets, Network, Sql), C++20 compiler.
