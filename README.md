# IRC-chat

An IRC-style chat server and client in C using the POSIX socket API over TCP/IP, with TLS encryption and bcrypt password hashing.

## Features

- **Multi-client server** — concurrent connections using `poll()`, single-process, no threads
- **TLS encryption** — all client-server traffic encrypted via OpenSSL, preventing network sniffing
- **bcrypt password hashing** — passwords stored as bcrypt hashes, never in plaintext
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
- **Graceful shutdown** — `SIGINT` handler cleans up sockets, SSL contexts, and memory

## Security

### Transport — TLS

All client-server communication is wrapped in TLS. Without it, every message, password, and file is visible in plaintext to anyone sniffing the network (verifiable with `tcpdump -A` or we can check it via `Wireshark`). TLS encrypts the TCP stream using OpenSSL, making intercepted traffic unreadable.

P2P file transfers between clients use raw TCP — the server-side TLS protects the transfer negotiation (who sends, who accepts, the port), while the file data travels directly between peers. Encrypting P2P would require each client to act as a TLS server with its own certificate.

### Passwords — bcrypt

Passwords are hashed with bcrypt (cost 12) before storage. bcrypt is intentionally slow (~240ms per hash), making brute-force attacks impractical compared to fast hashes like SHA-256 (~0.001ms). The salt is generated per-user from `/dev/urandom` and embedded in the hash string, preventing rainbow table attacks.

## Protocol

All messages use a fixed-size binary `struct message` (392 bytes) followed by an optional variable-length payload. Multi-byte fields are transmitted in network byte order (`htonl`/`ntohl`).

The fixed-size struct simplifies parsing — the receiver always reads exactly 392 bytes — but wastes bandwidth since unused fields (e.g., `login` and `password` on a chat message) are sent as zeros. A production implementation would use variable-length encoding with length-prefixed fields to reduce overhead.

### Message flow

```
Client                              Server
  |                                    |
  |------------ TCP connect ---------->|
  |<----------TLS handshake ---------->|
  |                                    |
  | ----struct message (encrypted) --->|  392 bytes header
  | ---------payload (encrypted) ----->|  pld_len bytes (if any)
  |                                    |
  |<---struct message (encrypted) -----|
  |<----payload (encrypted) -----------|
```

## Architecture

```
client.c            CLI client, poll()-based I/O, TLS connection
server.c            Server loop, client linked list, TLS accept, message dispatch
common.h            Protocol helpers — TLS (send_msg/recv_msg) and raw (send_msg_raw/recv_msg_raw)
msg_struct.h        Message types enum and struct definition
user_manager.c/h    bcrypt password hashing, registration, authentication, file persistence
history_manager.c/h Per-user chat history logging and retrieval
```

## Build

```
sudo apt install libssl-dev libcrypt-dev
make
```

Compiles with `gcc -Wall`. Links `-lssl -lcrypto` (TLS) and `-lcrypt` (bcrypt).

### Generate TLS certificate (required, run once)

```
openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt -days 365 -nodes -subj "/CN=localhost"
```

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
| `/nick <n>` | Set or change nickname |
| `/who` | List connected users |
| `/whois <user>` | Show user connection info |
| `/msg <user> <text>` | Private message |
| `/msgall <text>` | Broadcast to all users |
| `/sendfile <path>` | Upload file to server |
| `/listfiles` | List server files |
| `/getfile <n>` | Download file from server |
| `/send <user> <path>` | P2P file transfer |
| `/ban <user>` | Ban user (admin only) |

## Runtime data

```
.resa1/
├── history/       # per-user chat logs (<username>.log)
├── files/         # server-side uploaded files
└── downloads/     # client-side downloaded files
```

## License

MIT
