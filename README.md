# amalgame-pollen

> Workflow engine for [Pollen](https://github.com/amalgame-lang/pollen) — runs `workflow.json` **v3** entries over a P2P TCP message bus.

This package extracts the workflow runtime that originally lived inside the `pollen` CLI binary, so it can be embedded :

- by Mosaic web apps that want to trigger workflows from HTTP routes — or **act as a node in the middle of a workflow** (route inbound HTTP → `Pollen.Publish`, or run `Pollen.StartListener` to receive messages and bridge to external systems)
- by any Amalgame program that needs the v3 dispatcher (`if` / `for` / `while` / `set` / `goto` / `call` / `anchor` over a flat list of entries)

## Status

**v0.3.0** — internal cleanup of dead v2 C helpers (~2170 LOC). No
API change vs v0.2.0.

**v0.2.0** — v3 dispatcher feature-complete. The v1/v2 dispatcher
(workflow-tree schema, cond branches, debug bridge, Mosaic
`OnMessage`/`OnComplete`/`Forward` bridge) was retired. Workflows
now describe themselves with the v3 schema :

```json
{
  "schema": "pollen/v3",
  "actions": { "fetch": { "topic": "data.fetch" } },
  "entries": [
    { "name": "via-cron", "on": "tick.daily",
      "do": [ { "type": "goto", "target": "shared-pipeline",
                "args": ["state.dataset"], "bind": "state.summary" } ] },
    { "name": "shared-pipeline", "params": ["dataset"],
      "returns": "state.summary",
      "do": [
        { "type": "call", "action": "fetch", "on_error": "fail" },
        { "type": "anchor", "name": "after-fetch" },
        { "type": "for", "var": "item", "in": "state.items",
          "do": { "type": "if", "cases": [
            { "when": "item.priority > 5",
              "do": { "type": "call", "action": "alert", "mode": "all" } },
            { "else": true,
              "do": { "type": "call", "action": "store" } } ] } },
        { "type": "set", "key": "state.summary",
          "value": "\"processed-\" + string(len(state.items))" } ] }
  ]
}
```

Dispatched in-process by the AM-side `PollenDispatcher` (tree-walking
interpreter, 64-frame call stack, CEL-lite expression language for
`when` / `value` / `in` / `cond` fields). The C-side handles topic
lookup + GC-stable message duplication + Live executions recording ;
the AM-side walks the AST, evaluates expressions, mutates the
per-message state blackboard, resolves `call`s through the capability
registry (power-of-two LB), and pops frames on `goto` returns.

| Component | Status |
|---|---|
| CEL-lite lexer + Pratt parser + tree-walking evaluator | ✅ |
| v3 validator (8 rules, including DFS cycle detection) | ✅ |
| v3 loader (JSON → AST pool + name resolver) | ✅ |
| Tree-walking dispatcher (set / goto / anchor / if / for / while / call) | ✅ |
| Listener fork (bus topic → entry dispatch) | ✅ |
| Capability discovery + power-of-two load balancer | ✅ (shared from v0.1.x) |
| Live executions recorder (manager Live panel hops) | ✅ |
| `call.on_error` policies (`log` / `fail` / `drop`) | ✅ |

Smoke suites in CI (`tests/run_tests.sh` for C-side, the `tests/build-*-smoke.sh` for AM-side) :

- **cel_lite_smoke** — 83 assertions (lex / parse / arithmetic / compare / logical / string / `in` / list indexing / env-backed paths)
- **v3_validator_smoke** — 9 clean fixtures + 1 negative (cycle detection)
- **v3_loader_smoke** — 67 assertions (entries / anchors / actions / AST shape)
- **v3_dispatch_smoke** — 35 assertions (linear / branching / loops / goto+params+returns / call audit + `on_error`)
- **v3_listener_smoke** — 7 assertions (real TCP socket + topic→entry routing + dispatch count)
- **header_consumer_check** — 39 public symbols verified
- **listener_smoke** + **publish_smoke** — TCP transport baseline

## API surface

All public methods live on the singleton class `Pollen`.

### Shared infrastructure

```amalgame
// State location for the executions/ recorder + capabilities/ dir
Pollen.WorkflowSetSharedDir("/var/lib/myapp/pollen")
// Self identity for the Live executions panel
Pollen.WorkflowSetSelf("worker-a", "127.0.0.1", 8000)

// TCP transport — fire-and-forget + synchronous variants
let mid: string = Pollen.Publish("127.0.0.1", 8000,
                                   "tick.hourly", 1, "{\"who\":\"cron\"}")
let mid2: string = Pollen.PublishSync(host, port, topic, ver, data, 5000)

// Server side — accept loop, one pthread per connection
Pollen.StartListener(8000)
```

### Capability discovery + load balancer (Phase 6.1-6.3)

```amalgame
// Advertise this node's `actions` (consumed topics) under
// sharedDir/capabilities/<id>.json every 5s
Pollen.StartCapabilityWriter("worker-a", "127.0.0.1", 8000)
// Build an in-memory registry from the same dir (scan every 2s)
Pollen.StartCapabilityReader()
// Toggle registry-resolved forwarding (power-of-two-choices pick)
Pollen.SetLoadBalance(true)
// Introspection
let n: int = Pollen.RegistrySize()
let s: string = Pollen.ResolveProvider("data.fetch")  // "host:port" or ""
```

### v3 dispatcher

```amalgame
// Load + validate a v3 workflow
let errs: List<string> = Pollen.WorkflowValidateV3("workflow.json")
if (errs.Count() == 0) {
  let ok: bool = Pollen.WorkflowLoadV3("workflow.json")
}

// Dispatch an entry directly (typically you'd let the listener
// fork route bus messages to the matching entry)
let env: string = "{\"data\":{...}}"
let rc: int = Pollen.WorkflowV3DispatchEntry(0, env)

// Dispatch by topic — the same path the listener takes
let rc2: int = Pollen.WorkflowV3DispatchTopic("tick.hourly", env)

// Introspection (for the pollen-manager tree view)
let n: int = Pollen.WorkflowV3EntryCount()
for i in 0..n {
  let name: string = Pollen.WorkflowV3EntryName(i)
  let root: int = Pollen.WorkflowV3EntryDoRoot(i)
  // …walk WorkflowV3Node{Kind,Child0,Next,Name,Expr,Bind,…}
}
```

The CEL-lite spec : `docs/proposals/pollen-v3-cel-lite.md`.
The v3 dispatcher spec + validator rules : `docs/proposals/pollen-v3.md`.

## Limitations (v0.3.x)

- Single-engine per process — multi-engine hosting (one process,
  two independent workflows on different port ranges) is unscheduled.
- Mosaic bridge (`OnMessage` / `OnComplete` / `Forward`) was
  v2-only and went with the dispatcher. The v3-shaped equivalent
  (hook into `PollenDispatcher` step callbacks) is unscheduled.

## Publishing checklist

1. Bump `version` in `amalgame.toml`
2. `git tag vX.Y.Z && git push origin main && git push origin vX.Y.Z`
3. Wait for CI green (`tests/run_tests.sh` runs all smoke tests against the rebuilt archive)
4. From the Amalgame compiler repo : `./tools/register-package.sh pollen vX.Y.Z`
5. Merge the auto-opened PR on `amalgame-lang/packages-index`
6. Verify : `amc package add pollen` resolves the new version

## License

Apache-2.0. See [LICENSE](LICENSE).
