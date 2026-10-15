# System link endpoint selection

`/services/link` retains the existing single endpoint configuration. A paired
connector may instead provide an ordered `endpoints` array (up to four entries).
Each entry has a unique `id` and the existing WS endpoint fields: host, port,
path, tls, verify_peer, optional ca_pem, headers, reconnect_ms and heartbeat settings (ping_interval_ms, pong_timeout_ms, pong_misses). Whole configuration
validation and persistence complete before replacing the prior applied setting.

```json
{"enabled":true,"endpoints":[
  {"id":"lan","host":"wishos-example.local","port":18762,"path":"/os/device?id=example","tls":false,"headers":{"Authorization":"Bearer pairing-grant"}},
  {"id":"relay","host":"relay.wishos.dev","port":443,"path":"/os/device?id=example","tls":true,"verify_peer":true,"headers":{"Authorization":"Bearer pairing-grant"}}
],"attempt_timeout_ms":20000,"retry_ms":5000,"probe_interval_ms":60000,"idle_ms":250}
```

Identity and pairing-grant provisioning belong to the user connector/relay. The
firmware does not infer a model provider or session from these endpoints. This is
transport policy, not a user work mode. The C3/C6 SDK resolves `.local` by mDNS;
connector hostnames can therefore remain stable across DHCP address changes.
The default proxy bypass includes `.local`; existing explicitly configured proxy
bypass rules remain unchanged and should include the chosen LAN hostname if it
must bypass an Internet proxy. No fixed IP or router port mapping is required.

Selection tries endpoints in order, advances after a failed attempt, and retries
from the start after the list is exhausted. While on a lower-priority endpoint,
a bounded native worker periodically tests higher-priority TCP reachability.
A positive probe is only reachability, not an authentication receipt. The selected
full connection still performs WS/TLS/authentication according to its settings.
Preferred promotion waits for the RPC stream/queues to become idle. A zero
probe_interval_ms disables proactive promotion. Attempt cancellation is cooperative;
its deadline does not imply an SDK DNS/TLS operation can be forcibly freed safely.

Reads expose desired/applied revision, selected endpoint id/index, connection,
closing, probe and memory-wait state. Queries do not connect or probe. Changes and
shutdown wait for native socket/probe cleanup. Reconfiguring over the current RPC
channel drains the response before closing that channel. In-flight uncertain
mutations must still be reconciled by the connector; switching does not replay them.
The wire format remains `wishos-rpc/1`, with the same boot/operation identities.

The WS adapter defaults to a 20-second ping interval, a 5-second pong timeout
and two missed pongs before disconnect. This detects silent broken TCP paths;
it is transport liveness, not proof the Harness is executing. AI task deadlines
handle an upstream process that still answers pings but never completes work.
