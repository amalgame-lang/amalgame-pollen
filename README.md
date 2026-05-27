# amalgame-pollen

> Workflow engine for [Pollen](https://github.com/amalgame-lang/pollen) — runs `workflow.json` v2 trees over a P2P TCP message bus.

This package extracts the workflow runtime that originally lived inside the `pollen` CLI binary, so it can be embedded :

- by the `pollen` CLI itself (thin wrapper, planned M3)
- by Mosaic web apps that want to trigger workflows from HTTP routes — or **act as a node in the middle of a workflow** (route inbound HTTP → `Pollen.Publish`, or run `Pollen.StartListener` to receive messages and bridge to external systems)
- by any Amalgame program that needs the `if` / `for` / `while` / `set` tree engine

## Status

**v0.1.8** — feature-complete vs the legacy runtime's non-debug paths. Migration milestones :

| Milestone | Scope | Status |
|---|---|---|
| M1 | Package skeleton (toml + header + facade stubs) | ✅ v0.1.0 / v0.1.1 |
| M2.1 | state_persist module (file r/w atomic, StateGet/StateSet) | ✅ v0.1.2 |
| M2.2 | expr_eval + cond_eval (EvalExpr / EvalCond) | ✅ v0.1.3 |
| M2.3a | Publisher hot path (Pollen.Publish real) | ✅ v0.1.4 |
| M2.3b | Listener + PublishSync (ACK echo) | ✅ v0.1.5 |
| M2.3c.1 | Workflow dispatch flat (consumes / nexts / forward) | ✅ v0.1.6 |
| M2.3c.2a | Cond branches + set state ops in dispatch | ✅ v0.1.7 |
| M2.3c.2b | For + while loop routes in dispatch | ✅ v0.1.8 |
| M2.4 | Debug bridge (pause / step / mutate + manager :3001) | next |
| M3 | Refactor `pollen` CLI to a thin wrapper around this package | planned |
| M4 | Refactor `pollen-manager` to call `Pollen.Publish` directly | planned |
| v0.2 | Multi-engine (one process hosts two workflows) | future |

78 smoke assertions green in CI as of v0.1.8 (`tests/run_tests.sh`).

## API surface

All public methods live on the singleton class `Pollen`.

### Per-execution state file

State lives under `<sharedDir>/state/<rootMessageId>.json` and survives crashes (atomic tmp + rename). `set` ops in the workflow tree write here ; `state.X` lookups in `if` conditions read it back.

```amalgame
Pollen.WorkflowSetSharedDir("/var/lib/myapp/pollen")
Pollen.StateSet(rootMid, "state.counter", "1")
let v: string = Pollen.StateGet(rootMid, "state.counter")  // "1"
```

### Standalone evaluators

The expression and condition evaluators that drive the workflow `set` and `if` steps are also exposed standalone — useful for testing or running ad-hoc rules.

```amalgame
let val: string = Pollen.EvalExpr(envelopeJson,
    "{\"op\":\"+\",\"left\":{\"var\":\"data.qty\"},\"right\":{\"const\":1}}")
// → "43" for envelope.data.qty == 42

let pass: bool = Pollen.EvalCond(envelopeJson,
    "{\"op\":\"and\",\"args\":[" +
      "{\"op\":\">\",\"var\":\"data.amount\",\"value\":1000}," +
      "{\"op\":\"in\",\"var\":\"data.user.tier\",\"values\":[\"vip\",\"gold\"]}" +
    "]}")
```

Grammar : `{const | var | op +-*/}` for expressions ; `{leaf op (==, !=, <, >, <=, >=) | and / or / not | in / not_in}` for conditions. See the `Pollen.EvalCond` smoke tests for the full coverage.

### TCP transport

```amalgame
// Fire-and-forget publish — returns the freshly minted messageId
let mid: string = Pollen.Publish("127.0.0.1", 8000,
                                   "order.in", 1, "{\"qty\":3}")

// Synchronous publish — blocks until the matching ACK comes back
let mid2: string = Pollen.PublishSync("127.0.0.1", 8000,
                                        "order.in", 1, "{\"qty\":3}",
                                        5000)  // 5 s timeout

// Server side — block forever, accepting + dispatching messages
Pollen.StartListener(8000)
```

`StartListener` runs an `accept()` loop + spawns one pthread per accepted connection. Each worker reads newline-delimited JSON envelopes, ACKs them, and forwards them per the loaded workflow.

### Workflow runtime

The package keeps `workflow.json` parsing out of scope (every consumer's schema can differ — `pollen` CLI uses v2, a Mosaic app might use a custom format). Instead the package exposes the **setters** the workflow loader calls. Bracket each load with the reload pair :

```amalgame
Pollen.WorkflowReloadBegin()        // takes the runtime mutex, clears

// flat call topology (Phase 3.x)
Pollen.WorkflowAddConsume("order.in")
Pollen.WorkflowAddNext("nodeB", 9000)
Pollen.WorkflowAddNext("nodeC", 9100)
Pollen.WorkflowSetEmitTopic("order.queued")

// Phase 5.2 conditional branches (`if`)
Pollen.CondBranchOpen(
    "{\"op\":\"==\",\"var\":\"data.user.tier\",\"value\":\"vip\"}")
Pollen.CondBranchAddTarget("vipQueue", 9200)
Pollen.CondBranchOpen("")                  // else branch
Pollen.CondBranchAddTarget("stdQueue", 9300)

// Phase 5.3 state mutations (`set`)
Pollen.SetOpAdd("state.processed", "{\"const\":true}")
Pollen.SetOpAdd("state.counter",
    "{\"op\":\"+\",\"left\":{\"var\":\"state.counter\"},\"right\":{\"const\":1}}")

// Phase 5.4 for / while loops
Pollen.ForSetup("item")
Pollen.ForAddTarget("worker", 9400)
Pollen.ForAddItem("\"a\"")
Pollen.ForAddItem("\"b\"")
Pollen.ForAddItem("\"c\"")
// — or —
Pollen.WhileSetup(
    "{\"op\":\"<\",\"var\":\"data.iter\",\"value\":10}",
    "_wf_iter",         // state key for the auto-iter counter
    100,                // maxIter safety cap
    "self.host", 8000)  // loop target = this listener
Pollen.WhileAddExit("downstream", 9500)

Pollen.WorkflowReloadCommit()        // releases mutex, bumps version
```

The listener's dispatch priority on each matched message :
1. Apply pending `set` ops (mutate state.X before cond eval reads it)
2. `while` route active → self-loop bounded by maxIter
3. `for` route active → fan out across items × targets
4. `cond` active → first-match-wins branch routing
5. Otherwise → fan out to every `next` (forward_all)

A drop of any feature on reload reverts to plain fan-out (no sticky state across reload).

## Embedding example

A Mosaic web app that routes inbound HTTP POSTs into a workflow :

```amalgame
import Amalgame.Pollen
import Amalgame.Web

public class OrderRoute {
    public static HttpResponse POST(ctx: WebContext) {
        let mid: string = Pollen.PublishSync(
            "127.0.0.1", 8000, "order.in", 1, ctx.Body, 5000)
        if (mid == "") {
            return HttpResponse.New().Status(503)
                .Json("{\"error\":\"upstream timeout\"}")
        }
        return HttpResponse.New()
            .Json("{\"messageId\":\"" + mid + "\"}")
    }
}
```

The same Mosaic app can also act as a workflow node in the middle of the chain : call `Pollen.WorkflowAddConsume` for an upstream topic, run `Pollen.StartListener` in a background pthread, and bridge incoming messages to external systems via your route handlers.

### Bridging to the outside world (v0.1.17)

Pollen itself stays a pure TCP/topic bus — the **adapter** to HTTP (or anything else) is ordinary AM code you write in a Mosaic app that embeds the package, hooked in via three primitives. The node receives a Pollen message, you do the outbound call, and the result re-enters the workflow.

```amalgame
import Amalgame.Pollen
import Amalgame.Net.Http   // your egress client

// EGRESS — transform a consumed message before it is forwarded.
// The handler gets the full envelope JSON and returns the new
// `data` JSON to forward ("" drops the message). Here it calls an
// external HTTP service and forwards the response as the new data.
Pollen.OnMessage(env => {
    let body: string = Json.GetString(env, "data")   // your accessor
    let resp: string = Http.PostJson("https://api.example.com/enrich", body)
    return resp                                       // becomes data.* downstream
})

// INGRESS reply — fires when an execution terminates at this (leaf)
// node. Resolve the pending HTTP response, correlating by rootMid.
Pollen.OnComplete(env => {
    let root: string = Json.GetString(env, "rootMessageId")
    Replies.Resolve(root, env)    // your per-request correlation map
    return ""
})

// OUT-OF-BAND re-emit — from a Mosaic HTTP-handler thread (NOT from
// inside OnMessage/OnComplete). Keeps rootMessageId, chains parent.
let mid: string = Pollen.Forward(savedEnvelope, "{\"approved\":true}")
```

The wiring (consume topic, nexts, emit topic, `StartListener` on a background pthread) is exactly the same as any node — `OnMessage` just inserts your transform between recv and forward. Handlers run on the listener worker thread under the routing lock, so they must **not** call `Pollen.Forward` (return data / use `Pollen.Publish` instead) ; `Forward` is for separate threads.

## Limitations (v0.1.x)

- **Singleton-per-process.** Engine state is in static C globals (matches the `pollen` runtime). v0.2 will optionally wrap in an opaque handle so one process can host multiple independent workflows.
- **No workflow.json parser in-package.** Every consumer parses their own schema and walks it through the setters above. The legacy `pollen` CLI's v2 parser will land in M3 as a thin layer on top.
- **Missing-state-X in cond returns false** (verbatim from upstream runtime, vs `eval_expr` which returns numeric 0). Reference `data.X` in `if` conditions to avoid the asymmetry, or seed state via `SetOp` before the cond fires.
- **Single-source class** while amc's multi-source package story stabilises ; sibling .am modules to be split out later if `facade.am` grows too large.

## Publishing checklist

1. Bump `version` in `amalgame.toml`
2. `git tag vX.Y.Z && git push origin main && git push origin vX.Y.Z`
3. Wait for CI green (`tests/run_tests.sh` runs all smoke tests against the rebuilt archive)
4. From the Amalgame compiler repo : `./tools/register-package.sh pollen vX.Y.Z`
5. Merge the auto-opened PR on `amalgame-lang/packages-index`
6. Verify : `amc package add pollen` resolves the new version

## License

Apache-2.0. See [LICENSE](LICENSE).
