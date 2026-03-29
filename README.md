# epnet

UDP client-server networking in C11. Connection management, reliable
delivery, event-driven API. Linux native, cross-compiles to Windows
with MinGW.

IPv4 only. 12-byte packet header with sequence/ack/ack_bits, a
reliability layer for ordered messages, and a challenge-response
handshake for connections.

## Design

The ack-bitfield reliability scheme is based on Glenn Fiedler's
[Reliable UDP](https://gafferongames.com/post/reliable_ordered_messages/)
series. Each packet header carries the sender's latest sequence number,
the highest remote sequence it has seen, and a 32-bit bitfield of prior
received sequences - giving both sides enough information to detect
loss and measure RTT without a separate ack channel. The reliable
message layer sits on top: messages are queued, piggybacked onto
outgoing packets, and resent until the carrying packet is acked.
No encryption.

## Building

Requires Make and a C11 compiler.

```sh
make                # debug, static + shared
make CFG=release    # release (-O2)
make static         # just .a files
make shared         # just .so/.dll
make OS=windows     # cross-compile with MinGW
make clean          # remove current config's build artifacts
make distclean      # rm -rf build/
make format         # clang-format all sources
make help
```

`CFG` (`debug`, `release`), `OS` (`linux`, `windows`), `ARCH` (`x64`, `x86`) and `CC`/`AR` are overridable.

Output: `build/<os>-<arch>-<cfg>/lib/`. Two libraries: `libepnet-client`
and `libepnet-server`, each bundling the common code. Link with `-lws2_32`
on Windows.

## Usage

Call `epnet_init()` at startup, `epnet_shutdown()` at exit.

### Client

```c
#include "epnet_client.h"

epnet_init();
epnet_client_t *c = epnet_client_create();
epnet_client_connect(c, "example.com", 27015);

while (running) {
    epnet_client_update(c, dt);

    epnet_event_t ev;
    while (epnet_client_poll_events(c, &ev)) {
        switch (ev.type) {
        case EPNET_EVENT_CONNECTED:    break;
        case EPNET_EVENT_DISCONNECTED: break;
        case EPNET_EVENT_PACKET:       /* ev.data.packet.{pkt_type, data, len} */ break;
        case EPNET_EVENT_RELIABLE:     /* ev.data.reliable.{data, len} */ break;
        }
    }

    epnet_client_send(c, 0x10, payload, len);         // unreliable
    epnet_client_send_reliable(c, data, data_len);    // reliable, max 256 bytes
}

epnet_client_destroy(c);
epnet_shutdown();
```

Packet types below `0x10` are reserved for the protocol.

To avoid blocking DNS, resolve ahead of time:

```c
epnet_address_t addr;
epnet_address_from_string("example.com", 27015, &addr);   // do on a worker thread
epnet_client_connect_addr(c, addr);                       // non-blocking
```

### Server

Same create/update/poll/destroy cycle, with a `client_id` on everything:

```c
#include "epnet_server.h"

epnet_init();
epnet_server_t *s = epnet_server_create(27015, 16);

while (running) {
    epnet_server_update(s, dt);

    epnet_srv_event_t ev;
    while (epnet_server_poll_events(s, &ev)) {
        switch (ev.type) {
        case EPNET_SRV_EVENT_CLIENT_JOIN:  /* ev.client_id */ break;
        case EPNET_SRV_EVENT_CLIENT_LEAVE: break;
        case EPNET_SRV_EVENT_PACKET:       /* ev.data.packet.* */ break;
        case EPNET_SRV_EVENT_RELIABLE:     /* ev.data.reliable.* */ break;
        }
    }

    epnet_server_send(s, client_id, 0x10, data, len);
    epnet_server_broadcast(s, 0x10, data, len);
    epnet_server_send_reliable(s, client_id, data, len);
    epnet_server_disconnect_client(s, bad_client);
}

epnet_server_destroy(s);
epnet_shutdown();
```

## API

Headers: [`include/epnet.h`](include/epnet.h),
[`include/epnet_client.h`](include/epnet_client.h),
[`include/epnet_server.h`](include/epnet_server.h).

Both client and server use opaque handles.

### Client

- `epnet_client_create()` - open a UDP socket, return handle or NULL
- `epnet_client_connect(c, host, port)` - resolve DNS + start handshake
- `epnet_client_connect_addr(c, addr)` - start handshake with a pre-resolved address
- `epnet_client_update(c, dt)` - recv, timeouts, heartbeats, reliable resends
- `epnet_client_send(c, type, data, len)` - unreliable packet
- `epnet_client_send_reliable(c, data, len)` - queued, resent until acked
- `epnet_client_poll_events(c, &ev)` - 1 if event available, 0 if empty
- `epnet_client_get_state(c)` - `DISCONNECTED` / `CONNECTING` / `CONNECTED`
- `epnet_client_get_rtt(c)` - smoothed RTT in seconds
- `epnet_client_get_id(c)` - slot ID assigned by the server
- `epnet_client_disconnect(c)` / `epnet_client_destroy(c)`

### Server

- `epnet_server_create(port, max_clients)` - bind + return handle
- `epnet_server_update(s, dt)` - recv, timeouts, heartbeats, reliable resends
- `epnet_server_send(s, client_id, type, data, len)` - to one client
- `epnet_server_broadcast(s, type, data, len)` - to all connected
- `epnet_server_send_reliable(s, client_id, data, len)` - reliable to one client
- `epnet_server_poll_events(s, &ev)` - 1 if event available, 0 if empty
- `epnet_server_disconnect_client(s, id)` - force-kick
- `epnet_server_destroy(s)`

### Address helpers

- `epnet_address_from_string(host, port, &addr)` - resolve hostname to `epnet_address_t`

`epnet_address_t` is defined in `epnet.h` (included by both public headers).

### Events

Events come from a ring buffer. The structs embed payload inline (~1.2 KB
each) - keep that in mind if you're running at scale.

Client events (`epnet_event_t`): `CONNECTED`, `DISCONNECTED`, `PACKET`, `RELIABLE`.
Server events (`epnet_srv_event_t`): `CLIENT_JOIN`, `CLIENT_LEAVE`, `PACKET`, `RELIABLE`.

Both carry `client_id` and a union of `packet.{pkt_type, data, len}`,
`reliable.{data, len}`, or `disconnect_reason` (client only).

### Compile-time knobs

Override with `-D` flags:

| Define | Default | Description |
|--------|---------|-------------|
| `EPNET_MAX_CLIENTS` | 16 | Max simultaneous connections |
| `EPNET_MAX_PAYLOAD` | 1220 | Max unreliable payload bytes |
| `EPNET_RELIABLE_MAX_DATA` | 256 | Max reliable message body |
| `EPNET_TIMEOUT_SEC` | 10.0 | Connection timeout |
| `EPNET_HEARTBEAT_SEC` | 1.0 | Heartbeat interval |
| `EPNET_CONNECT_RETRY_SEC` | 0.5 | Connect retry interval |
| `EPNET_CONNECT_MAX_TRIES` | 10 | Max connect attempts |
| `EPNET_RELIABLE_RESEND_SEC` | 0.1 | Reliable resend interval |
| `EPNET_DISCONNECT_SENDS` | 3 | Redundant disconnect packets |
| `EPNET_EVENT_QUEUE_SIZE` | 64 | Event ring buffer capacity |

## Wire Protocol

12-byte header on every packet:

```
[0..1]  protocol_id     0x5031, big-endian
[2..3]  sequence
[4..5]  ack             highest remote sequence seen
[6..9]  ack_bits        bitfield for 32 sequences before ack
[10]    type
[11]    reserved
```

Types `0x01`-`0x06` are protocol-internal. Application types start at
`0x10`. Max packet size on the wire is 1232 bytes.

### Handshake

Client sends `CONNECT_REQ` with a random 64-bit challenge token, retrying
every 0.5s up to 10 times. Server responds with `CONNECT_ACK` (challenge
echoed + assigned client ID) or `CONNECT_DENY` (reason: full, banned, or
version mismatch). Connections time out after 10s of silence; heartbeats
keep them alive.

### Reliable delivery

Each reliable message carries a `msg_id` and is resent every 100ms until
the transport packet it rode on gets acked. The receiver deduplicates via
a sliding-window bitfield. RTT is tracked with an exponential moving
average (alpha 0.1).

## Security

This is a transport layer, not a security protocol. Packets are plaintext.
The challenge-response handshake prevents blind spoofing from off-path hosts,
not on-path observation or replay.

- **No encryption.**
- **No authentication.**
- **No rate limiting.**

## Layout

```
include/
  epnet.h                     shared types, constants, library lifecycle
  epnet_client.h              public client API
  epnet_server.h              public server API
src/
  client/epnet_client.c
  server/epnet_server.c
  common/
    epnet_platform.{c,h}      sockets, time, random (POSIX + Win32)
    epnet_protocol.{c,h}      wire format, serialization
    epnet_connection.{c,h}    state machine, seq/ack tracking, reliability
```

## License

MIT - see [LICENSE](LICENSE).
