# Ordered AI results: device contract

Version: `wishos-ai/2` and `wishos-result/2` (2026-10-15).
The result contains ordered actions. Each action is either `application` or
`system`; there is no `both` target and no one-effect limit. This replaces v1.

Submit `/services/ai submit {input,request_id?,timeout_ms?}`. The upstream route
receives `{format:"wishos-ai/2",input}` with the existing request and boot identity.

Inline result:

```json
{
  "format": "wishos-result/2",
  "operation_id": "harness-operation-id",
  "transfer_id": 12,
  "actions": [
    {"type":"application","recipient":"origin","topic":"result","data":{"text":"The new reader is ready."}},
    {"type":"system","path":"/sys/apps","method":"install","args":{"manifest":{"$asset":"app.json"}}},
    {"type":"system","path":"/sys/apps","method":"launch","args":{"id":{"$previous":"application_id"}}},
    {"type":"application","recipient":"foreground","topic":"open","data":{"file":{"$asset":"book.txt"}}}
  ]
}
```

`transfer_id` is supplied by the Hub after verified resource delivery. Omit it for
results without staged assets. `$asset` resolves within that immutable bundle;
`$previous` reads the previous step's result (slash-separated object keys; an
empty string selects the entire result). They are structured values, not string
interpolation or executable expressions. Complex conditions and variables belong
to the existing runtime application language, not a second hidden scripting VM.

System actions use any advertised resource path/method with an object `args`.
The system waits for that service's declared completion if it is asynchronous.
A synchronous configuration receipt may distinguish desired/applied state; it
does not magically become a physical-completion guarantee. Device BUSY and active
native operations remain respected. Native power-off/reset can end the current
boot; the sequence is not a durable cross-reset program.

Application actions have `recipient: origin|foreground` (default origin), an
optional topic (default result), and object data. Origin is the exact submitting
VM instance. Foreground explicitly selects the currently loaded VM at that step,
including an application launched by an earlier system step. An absent recipient
fails that step; results are never implicitly retargeted after an application exits.

Default `wait: processed` publishes `application_action` with `{id,step,topic}`.
The recipient reads `/services/ai read_action {id,step}`, processes its action, then
calls `acknowledge {id,step,ok,result?}`. Only that runtime instance may acknowledge.
The action remains readable until acknowledgement, failure or cancellation; the
ordinary event is a wake-up hint, not the authority. The recipient VM
can inspect its pending tasks and recover a missed notification from the service.

Explicit `wait: queued` places small data into the event and immediately advances.
This confirms queueing only, not processing, so do not use it as a barrier before
replacing the recipient. Its payload must fit the advertised event envelope.

Large result sequences use a verified JSONL file instead of inline actions:

```json
{
  "format":"wishos-result/2",
  "operation_id":"harness-operation-id",
  "transfer_id":12,
  "sequence":{"path":"actions.jsonl","format":"jsonl"}
}
```

Each nonempty line is one action. There is no sequence-count constant. The file is
read incrementally, and a single action has a configurable byte budget (default
2048, discovered from the service). Control replies remain bounded. Whole sequence
structure/resource-method availability is preflighted before executing its first
action; state-dependent arguments are resolved/validated at their execution step.
The bundle holds a read lease while executing; a new transfer can proceed in a
different version directory. The overall task deadline is configurable.

Failure stops the remaining steps. Already completed effects are not rolled back
or replayed. Cancellation waits for an in-flight native operation to reach its
normal completion before stopping; it does not interrupt a panel waveform. Reads
expose state, step/count, cancellation and error. `read_result {id,offset,length}`
returns bounded hex chunks of the latest step's JSON result, including a failed
service's diagnostic result. A failed step does not increment the completed count. Final completion,
resource acceptance, application processing and physical display completion remain
different receipts. Same-operation duplicates share the authoritative execution;
a duplicate/follower failure must not replace that authoritative receipt.
