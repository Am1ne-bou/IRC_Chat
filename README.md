# irc-chat

An IRC-style chat server and client in C using the POSIX socket API over TCP/IP.

Supports multi-client connections via `poll()`, login/password authentication, private and broadcast messaging, server-side file storage, and peer-to-peer file transfers.

## Features

- **Multi-client server** — concurrent connections using `poll()`, single-process, no threads
- **Authentication** — login/password with auto-registration, persistent across restarts
- **Nicknames** — unique, alphanumeric-only, changeable at runtime
- **Private messaging** — `/msg <user> <text>`
- **Broadcast** — `/msgall <text>` to all connected users
- **User discovery** — `/who` lists online users, `/whois <user>` shows IP/port/connection time
- **Chat history** — per-user message logs saved to disk, replayed on login
- **Server file storage** — upload (`/sendfile`), list (`/listfiles`), download (`/getfile`)
- **P2P file transfer** — direct client-to-client transfer via `/send <user> <file>`
- **Admin controls** — ban users with `/ban <user>` (admin account only)
- **IPv4/IPv6** — dual-stack via `getaddrinfo()`
- **Graceful shutdown** — `SIGINT` handler cleans up sockets and memory

## Protocol

All messages use a binary protocol in two steps:

1. A fixed-size `struct message` header (type, sender, payload length, metadata)
2. An optional variable-length payload of `pld_len` bytes

Multi-byte fields are transmitted in network byte order (`htonl`/`ntohl`). The `send_msg()`/`recv_msg()` helpers in `common.h` handle serialization and guarantee complete transmission via loop-based `send_all()`/`recv_all()`.

## Architecture

```
client.c            CLI client with poll()-based I/O multiplexing
server.c            Server loop, client linked list, message dispatch
common.h            Protocol helpers (send_msg, recv_msg)
msg_struct.h        Message types enum and struct definition
user_manager.c/h    User registration, authentication, file persistence
history_manager.c/h Per-user chat history logging and retrieval
```

The server stores client state (socket fd, address, nickname, auth status, per-client upload state) in a singly linked list. Each node tracks its own in-progress file upload, preventing corruption from concurrent uploads.

## Build

```
make
```

Compiles with `gcc -Wall -Wextra -pedantic -std=c11`. Requires a POSIX system (Linux/WSL).

## Usage

Start the server:
```
./server 8080
```

Connect a client:
```
./client localhost 8080
```

### Commands

| Command | Description |
|---|---|
| `/login <user> <pass>` | Authenticate (auto-registers on first use) |
| `/nick <name>` | Set or change nickname |
| `/who` | List connected users |
| `/whois <user>` | Show user connection info |
| `/msg <user> <text>` | Private message |
| `/msgall <text>` | Broadcast to all users |
| `/sendfile <path>` | Upload file to server |
| `/listfiles` | List server files |
| `/getfile <name>` | Download file from server |
| `/send <user> <path>` | P2P file transfer |
| `/ban <user>` | Ban user (admin only) |

## License

MIT