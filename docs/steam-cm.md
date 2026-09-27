# Steam CM protocol

The client reaches the GC the same way it does through Steam: `libsteam_api`
keeps a connection to a CM (connection manager) and GC messages travel inside
`ClientToGC` / `ClientFromGC`. `gcserver` is that CM with the GC behind it.
Layouts and message numbers follow [SteamKit2](https://github.com/SteamRE/SteamKit).

```
game -> ISteamGameCoordinator::SendMessage -> steam_api (CCMClient)
     -> TCP -> gcserver (CCMServer) -> CDOTAGCServer
```

## Framing

```
[u32 length][u32 magic 'VT01'][payload]
```

The payload is encrypted after the channel handshake. Two payload forms:

```
protobuf:  [u32 EMsg | 0x80000000][u32 header length][CMsgProtoBufHeader][body]
simple:    [u32 EMsg][u64 target job][u64 source job][body]
```

Only the `ChannelEncrypt*` messages use the simple form. `CMsgProtoBufHeader`
here is Steam's (`steammessages_base.proto`), not the GC header of the same
name. Used fields: `steamid` (1), `client_sessionid` (2), `routing_appid` (3),
`jobid_source` (10), `jobid_target` (11), `eresult` (13).

## Session

| EMsg | id | direction | body |
|---|---|---|---|
| ChannelEncryptRequest | 1303 | server -> client | protocol 1, universe 1, 16-byte challenge |
| ChannelEncryptResponse | 1304 | client -> server | protocol 1, key size, RSA(session key + challenge), CRC32, 0 |
| ChannelEncryptResult | 1305 | server -> client | EResult |
| ClientLogon | 5514 | client -> server | `protocol_version` (1), `account_name` (50) |
| ClientLogOnResponse | 751 | server -> client | EResult, heartbeat interval; SteamID and session id in the header |
| ClientHeartBeat | 703 | client -> server | empty, every 9 s |
| ClientToGC / ClientFromGC | 5452 / 5453 | both | `CMsgGCClient` |
| ClientLogOff / ClientLoggedOff | 706 / 757 | | |
| Multi | 1 | both | `CMsgMulti` |

The server drops a session after 27 seconds without traffic.

`CMsgGCClient`: `appid` (1), `msgtype` (2, with the GC protobuf flag),
`payload` (3), `steamid` (4). The payload is the GC packet exactly as the game
passed it to `SendMessage`:

```
[u32 type | 0x80000000][u32 header length][GC CMsgProtoBufHeader][body]
```

The game leaves `client_steam_id` out of the GC header. The CM appends it
(field 1, fixed64) before handing the message to the GC, as Steam does.

`CMsgMulti`: `size_unzipped` (1), `message_body` (2). The body is
`[u32 length][payload]` repeated, gzipped when `size_unzipped` is set. The
server batches several replies to one request into a Multi and compresses
bundles from 1 KB up.

## Encryption

The client picks a random 32-byte AES key and sends it, followed by the
server's challenge, encrypted with RSA-OAEP (SHA-1) under the server's public
key. After `ChannelEncryptResult` every payload is

```
iv  = HMAC-SHA1(key[0:16], random3 + plain)[0:13] + random3
out = AES-256-ECB(key, iv) + AES-256-CBC(key, iv, plain)
```

and the receiver checks the HMAC after decrypting.

Steam's own public key does not help here, so `gcserver` generates an
RSA-1024 pair on first start (`build/cm_rsa.der`, `build/cm_rsa.pub.der`).
`run_client.sh` passes the public one to steam_api via `DOTA_CM_PUBKEY`.

## Accounts

There are no passwords. The account id is `CRC32(account name) & 0x7fffffff`
and the SteamID is `76561197960265728 + account id`. steam_api computes the same
SteamID locally, so it is known before the logon completes.

## steam_api settings

| variable | default |
|---|---|
| `DOTA_ACCOUNT` | `player` |
| `DOTA_CM_HOST` | `127.0.0.1` |
| `DOTA_CM_PORT` | `27017` |
| `DOTA_CM_PUBKEY` | `cm_rsa.pub.der` (`run_client.sh` sets it) |
| `DOTA_GC_HEXDUMP` | unset; set it to dump outgoing GC packets |

Without a CM the game still starts; GC sends fail with `k_EGCResultNotLoggedOn`
and steam_api retries the logon every 5 seconds.
