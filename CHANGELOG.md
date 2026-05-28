# Changelog

## v0.1.21 — 2026-05-27

### Added — v2 `for` routing (registry-resolved fan-out per item)
- `Pollen.ForSetTopic(topic)` — set the `for` route's emit topic. When set (+ LB on), each iteration rebuilds with that topic and forwards to a live provider resolved by power-of-two — no static targets. Items spread across replicas of the `do` action. The `while > for > cond > forward_all` dispatch priority is unchanged. v1 (`ForAddTarget` + static fan-out) still works when no topic is set.
- 35 public symbols declared. 156 assertions total.

### Note
- The reference node `RunV2` now wires `for` trees (`for { var, in, do }` → `ForSetup` + `ForSetTopic(actionTopic(do.action))` + `ForAddItem` per item). `while` v2 is the next slice.


## v0.1.20 — 2026-05-27

### Added — v2 conditional routing by topic (registry-resolved `if` branches)
- `Pollen.CondBranchSetTopic(topic)` — give a cond branch its own emit topic. When set (and load balancing is on), the matched branch rebuilds the envelope with **that** topic (each branch targets a distinct action) and forwards to a live provider resolved by the power-of-two LB — no static `host:port`. This is the `if` half of v2 registry routing : a workflow's `if` branches name abstract actions, and the binding to a running instance comes from discovery, exactly like the linear `call` path.
- Branches without a topic keep v1 behaviour (static `CondBranchAddTarget` + the global emit topic), so existing cond workflows are unchanged.
- `tests/lb_smoke.c` gains 2 assertions (8 total) : a topic-set branch routes to a registry-resolved provider and rebuilds with the branch topic. 156 assertions total across 13 files.

### Note
- This is the runtime half. The reference node (`examples/pollen-node.am` `RunV2`) wires v2 `if` trees onto it (branch `then` → action → `CondBranchSetTopic`), and the manager visualises the conditional routing live. `for`/`while` in v2 are the next slice.


## v0.1.19 — 2026-05-27

### Fixed — consumer header signature for `SetLoadBalance`
- `Amalgame_Pollen.h` declared `Amalgame_Pollen_Pollen_SetLoadBalance(int on)`, but amc emits the public symbol from the AM `on: bool` parameter as `void(code_bool)`. A **consumer** that imports the package (whose amc-generated C pulls in both the header decl and amc's own decl) hit a hard `conflicting types` compile error. Header now declares `code_bool on`. Caught by building the reference node (`examples/pollen-node.am`) — the package's own `--lib` build and `tests/*.c` never exercise an amc-generated consumer of the AM methods, so `header_consumer_check.c` (which only takes symbol addresses — name-only linkage) passed despite the mismatch. Same regression *class* as the v0.1.10 `#include "Amalgame.h"` bug. Followup : add a reference-node build to CI to make this a hard gate.

### Added — reference node `--load-balance` flag
- `examples/pollen-node.am` now calls `Pollen.StartCapabilityReader()` whenever a `--shared-dir` is set (keeps the registry view warm) and accepts `--load-balance` to call `Pollen.SetLoadBalance(true)`. Run several instances of the same role (same consume topic, different ports, shared `--shared-dir`, `--load-balance`) and the forward spreads across them via power-of-two.


## v0.1.18 — 2026-05-27

### Added — Phase 6.2/6.3 (capability registry reader + power-of-two load balancer)
- `Pollen.StartCapabilityReader()` — spawns a detached thread that scans the sharedDir `capabilities/` dir every 2s, parses each provider's `{host, port, actions[], load.inFlight, heartbeat}`, drops stale entries (heartbeat > 15s old), and atomic-swaps an in-memory registry (own mutex, independent of the workflow mutex). The read half of discovery, complementing the v0.1.13 writer.
- `Pollen.SetLoadBalance(on)` — toggles load-balanced forwarding. When **on**, a forward resolves the emit topic against the registry and sends to a **single** provider chosen by **power-of-two-choices** (sample 2, lower `inFlight` wins) instead of the static nexts. When **off** (default) or when no provider advertises the topic, the static nexts are used verbatim — so single-instance setups are byte-for-byte unchanged.
- `Pollen.ResolveProvider(topic) → "host:port"` — runs the same power-of-two pick and returns the winner (or `""` if none). For the manager's LB preview + deterministic testing.
- `Pollen.RegistrySize() → int` — number of live providers currently in the registry (post staleness filter). Introspection for tests + the manager.

### Design note
- This wires LB onto the **existing** workflow schema (the emit topic *is* the action a downstream consumes ; replicas of a role advertise the same topic). No schema change — the upcoming v2 `action:` schema + migration tool build on top. Keeps the demo green while the LB mechanics land.
- `tests/lb_smoke.c` — 6 assertions : registry build + staleness exclusion, deterministic lower-inFlight pick (+ sole-provider + unknown-topic `""`), and an end-to-end LB forward that lands on the resolved provider rather than the static next. 154 assertions total across 13 files.


## v0.1.17 — 2026-05-27

### Added — Mosaic bridge hooks (egress/ingress to the outside world)
- `Pollen.OnMessage(handler: Closure<string, string>)` — register an AM transform invoked for every message this node **consumes**, before it is forwarded. The handler receives the full envelope JSON and returns the **new `data` JSON** to forward (`""` → drop the message). The worker then forwards as usual (mid/parent/topic rewrite, cond/for/while routing all still apply). This is the **egress bridge** : a Mosaic app embedding Pollen does the outbound HTTP call (amalgame-net-http) inside the handler and returns the response as the new data — Pollen stays pure-TCP, the adapter lives in user AM code.
- `Pollen.OnComplete(handler: Closure<string, string>)` — fires when a consumed message **terminates at this node** (a leaf : no nexts / cond / for / while wired). The handler receives the final envelope JSON (return ignored). Use it on an ingress node to resolve a pending HTTP response, correlating by `rootMessageId`.
- `Pollen.Forward(envelopeJson, newDataJson) → mid` — re-emit a message into the workflow **out-of-band**, preserving the chain : fresh `messageId`, `parentMessageId` = the incoming mid, `rootMessageId` kept, `data` swapped to `newDataJson` (`""` keeps the original), topic rewritten to the configured emit topic, forwarded to all wired nexts. For deferred re-emission from a Mosaic HTTP-handler thread.

### Changed
- Listener-worker + capability-writer threads now spawn via `GC_pthread_create` (was raw `pthread_create`) so they are bdwgc-registered — required now that workers invoke AM closures (OnMessage/OnComplete) which allocate GC memory.

### Notes / limitations
- OnMessage/OnComplete handlers run **on the worker thread under the routing mutex**, so they must NOT call `Pollen.Forward` (re-lock → deadlock) — return the data, or use `Pollen.Publish`, instead. `Forward` is for *other* threads (a Mosaic HTTP handler). Same precedent as the M4 debug bridge, which already does network IO under that mutex.
- `tests/bridge_smoke.c` — 16 assertions : transform-then-forward, `""`-drop, leaf OnComplete fire (+ rootMessageId/data carried), out-of-band Forward (data swap + topic rewrite + parent chain). 148 assertions total across 12 files.


## v0.1.16 — 2026-05-27

### Added — M5 (nested-tree role resolution)
- `examples/pollen-node.am` now resolves a role's routing wherever it lives in the tree, not just the top-level sequence. New recursive `FindRoleSequence` returns the steps list that directly contains the role (descending into `if` branches' `then` and `for` bodies) ; `WireSetsAfter` / `FindActionAfter` / `WireWhile` operate on that containing sequence.
- A role buried in an `if` branch (e.g. `then: sequence [call vip, call audit]`) now correctly gets its next wired (vip → audit) — previously it fell out of the top-level walk and became terminal.
- Verified e2e : `ingest → if amount>1000 then [vip → audit]` routes `ingest → vip → audit` (vip nested), audit terminal.


## v0.1.15 — 2026-05-27

### Fixed — reference node accepts both node container shapes
- `examples/pollen-node.am` now resolves nodes from a `workflow.json` whose `nodes` is **either** an object keyed by role **or** an array of `{id, label, …}` entries. The pollen-manager's Save writes the **array** form, so a node restarted against a manager-saved workflow used to fail role resolution (it only handled the object form). New `FindNode` / `NodeKeys` helpers abstract over both (array → match `label`, then `id`).
- Verified end-to-end : a manager-saved (array) demo workflow routes correctly through package nodes — `amount=1500 → vip`, `amount=50 → standard`.


## v0.1.14 — 2026-05-27

### Added — Phase 6.2 (load metrics)
- The capability file now carries a `load` object : `{inFlight, msgsHandled}`. `inFlight` is bumped around each dispatch window, `msgsHandled` is the cumulative consumed count. Both are `_Atomic long` so the writer thread reads them without the workflow mutex.
- The pollen-manager discovery panel shows these per node ("inFlight N · handled M"). After injecting messages you can watch each node's `handled` climb live — and `inFlight` is the primary signal for the upcoming power-of-two load balancer (Phase 6.3).


## v0.1.13 — 2026-05-27

### Added — Phase 6.1 (capability advertisement / discovery)
- `Pollen.StartCapabilityWriter(label, host, port)` — spawns a detached thread that rewrites `<sharedDir>/capabilities/<instanceId>.json` every 5s with `{instanceId, host, port, label, actions (= consumed topics), heartbeat, version}`, atomic tmp+rename. Idempotent.
- This is the node-side half of discovery : the pollen-manager's Infra panel reads these files (`/api/capabilities`) and shows nodes alive / stale (heartbeat > 15s) cross-referenced against the declared `infrastructure.json`.
- `examples/pollen-node.am` calls it after `WorkflowReloadCommit` (when a shared dir is configured), so a package-driven mesh self-advertises out of the box.


## v0.1.12 — 2026-05-27

### Added — M4 (debug-aware publish) + consumer-build guard
- `Pollen.PublishDebug(host, port, topic, ver, dataJson, session, mode, breakpointsJson, managerAddr) → mid` — builds a MESSAGE envelope carrying the `debug` field (session / mode / manager / breakpoints / hit_bp) so a stepping run phones home from each paused hop. Centralises the envelope the pollen-manager hand-rolled in `/api/inject`. `messageId == rootMessageId` (originator, which the manager's hand-rolled version omitted). 9 smoke assertions (`tests/publish_debug_smoke.c`).
- `tests/header_consumer_check.c` — compile + link guard that the public `Amalgame_Pollen.h` is includable by a consumer (takes the address of all 26 public entry points). This is the test that would have caught the v0.1.10 `#include "Amalgame.h"` regression ; the package's own `--lib` build never exercised the header from a consumer's POV. 132 assertions total across 11 files.


## v0.1.11 — 2026-05-27

### Added — M2.4b (executions/ step recorder)
- Package nodes now write one `<sharedDir>/executions/<mid>-<role>.json` record per forwarded hop, so the pollen-manager "Live executions" panel populates from package-driven nodes (not just the legacy binary).
- `Pollen.WorkflowSetSelf(role, host, port)` now stores `host` + `port` (was role-only) — needed for the record's `node` field.
- The forward helpers (`forward_all` / `cond_forward` / `for_forward` / `while_forward`) thread the rebuilt envelope's fresh mid back to the listener, which records `{messageId, parentMessageId, role, topicIn, topicOut, nextCount, node, timestamp, debugSession?}`. Best-effort (`O_CREAT|O_EXCL`, skips dupes/fs errors). Chains reconstruct by walking `parentMessageId`.
- 7 new smoke assertions (`tests/recorder_smoke.c`). 97 total across the suite.


All notable changes to `amalgame-pollen`. Format inspired by
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) ;
versioning follows the upstream package indexing convention.

## v0.1.10 — 2026-05-27

### Fixed
- `runtime/Amalgame_Pollen.h` included a stray `"Amalgame.h"` (no such file in the amc runtime dir), which broke **any consumer** whose generated C pulls in the header — `fatal error: Amalgame.h: No such file`. The package's own `--lib` build never tripped it because `facade.c` includes `_runtime.h` directly. Now includes `"_runtime.h"` + `<stdint.h>` like every other package header. This unblocks building a program that does `import Amalgame.Pollen` (e.g. `examples/pollen-node.am`).

### Added
- `examples/pollen-node.am` — reference Pollen node binary built entirely on the package (the M3 "thin wrapper"). Parses a `workflow.json`, resolves the role, wires the runtime via the public setters, and blocks in `Pollen.StartListener`. Unlike the legacy binary, the `if` cond is evaluated correctly at dispatch.
  - M3.1 : consumes / emit / next (call, fan_out) / if-branches.
  - M3.2 : also wires `set` state ops, `for` loops (var + items + targets), and `while` loops (cond + maxIter + self-loop + exit targets). Verified end-to-end : `if` routes amount>1000→vip / else→standard ; `for ["a","b","c"]` fans out 3 messages to the worker.
  - Still top-level-sequence only (nested branches resolved one level for if/for `then`/`do` targets). Deeply nested trees are M3.3.

## v0.1.9 — 2026-05-27

### Added — M2.4 (debug bridge)
- Listener nodes now honour the debug protocol when an envelope carries a `debug` field. When this node's role matches the pause criteria (`mode=step`, or `mode=breakpoint` + role in `breakpoints[]`, or sticky `hit_bp:true`), the node phones home to the `manager` declared in the envelope (one-shot TCP `DEBUG_PAUSE` + blocking recv for the operator's command) and acts on the reply :
  - `DEBUG_CONTINUE` → rewrite `mode=breakpoint` `hit_bp=false`, forward.
  - `DEBUG_STEP_INTO` / `DEBUG_STEP_OVER` → rewrite `mode=step` `hit_bp=false`, forward (every downstream node pauses).
  - `DEBUG_MUTATE` → rewrite the `data` field from the reply, remap to `then` (continue / step), forward.
  - `DEBUG_CANCEL` / timeout / manager-unreachable → drop the message (the upstream ACK has already been sent).
- Self-role for breakpoint matching comes from `Pollen.WorkflowSetSelf(role, …)`.
- The debug bridge is entirely internal — no new public API. A node opts in simply by having `WorkflowSetSelf` called and receiving debug-tagged envelopes. Protocol matches `pollen-manager`'s `:3001` bridge so existing tooling works against package-driven nodes.
- 12 new smoke assertions (`tests/debug_bridge_smoke.c`) with a mock manager : continue / cancel / mutate / no-debug-passthrough.

### Not yet ported
- `executions/` step recorder (`_pollen_wf_record_step`) — the manager's "Live executions" panel won't populate for package nodes until M2.4b / v0.1.10.

## v0.1.8 — 2026-05-27

### Added — M2.3c.2b (for / while loops)
- `Pollen.ForSetup(itemVar)` / `ForAddTarget(host, port)` / `ForAddItem(jsonLit)` for fan-out over a finite item list.
- `Pollen.WhileSetup(condJson, iterKey, maxIter, selfHost, selfPort)` / `WhileAddExit(host, port)` for bounded self-loops.
- Listener dispatch priority : `while > for > cond > forward_all`.
- 7 new smoke assertions (`tests/for_while_smoke.c`).

### Known limitation
- `cond_eval_leaf` returns false when a `state.X` path is missing (verbatim from upstream runtime), while `eval_expr` returns numeric 0. Reference `data.X` in `if` conditions to avoid the asymmetry, or seed state via `SetOp` before the cond fires.

## v0.1.7 — 2026-05-27

### Added — M2.3c.2a (cond branches + set state ops)
- `Pollen.CondBranchOpen(condJson)` / `CondBranchAddTarget(host, port)` — first-match-wins routing using the same cond grammar as `Pollen.EvalCond`.
- `Pollen.SetOpAdd(path, valueExprJson)` — state mutations applied before the cond eval runs.
- 9 new smoke assertions (`tests/cond_set_dispatch_smoke.c`).

## v0.1.6 — 2026-05-27

### Added — M2.3c.1 (workflow dispatch flat)
- `Pollen.WorkflowReloadBegin()` / `WorkflowReloadCommit()` bracketing pair for atomic config swaps.
- `Pollen.WorkflowAddConsume(topic)` / `WorkflowAddNext(host, port)` / `WorkflowSetEmitTopic(topic)` setters.
- Listener integration : `topic ∈ consumes → ACK + forward to nexts`, otherwise drop.
- Envelope rebuild on forward (fresh `messageId`, `parentMessageId` chain, optional topic rewrite).
- `Pollen.WorkflowVersion()` now returns the real `_pollen_wf_version` (was a stub).
- 9 new smoke assertions (`tests/workflow_dispatch_smoke.c`).

## v0.1.5 — 2026-05-27

### Added — M2.3b (listener + sync publish)
- `Pollen.StartListener(port)` — blocks the calling thread, accept loop + pthread-per-conn, ACK echo (no dispatch yet — that arrives in v0.1.6).
- `Pollen.PublishSync(host, port, topic, ver, data, timeoutMs)` — same shape as `Publish` but waits for the matching ACK.
- Two pollen nodes can now talk MESSAGE↔ACK using only the package — the legacy `pollen` binary is no longer required for basic transport.
- 7 new smoke assertions (`tests/listener_smoke.c`).

## v0.1.4 — 2026-05-27

### Added — M2.3a (publisher hot path)
- `Pollen.Publish(host, port, topic, ver, data)` drops its stub — real connect / mint UUIDv4 / format envelope / send / close.
- `_pollen_gen_uuid` RFC 4122 v4 helper (getrandom + version/variant bits).
- Envelope shape matches the runtime's `EncodeMessage` so v0.1.4 publishers interop with any existing pollen node.
- 11 new smoke assertions (`tests/publish_smoke.c`).

## v0.1.3 — 2026-05-27

### Added — M2.2 (expr + cond evaluator)
- `Pollen.EvalExpr(envelopeJson, exprJson) → string` — evaluates `{const | var | op +-*/}` expressions against an envelope. Returns the literal form ("42", "\"foo\"", etc).
- `Pollen.EvalCond(envelopeJson, condJson) → bool` — recursive cond walker : leaf (`==`, `!=`, `<`, `>`, `<=`, `>=`) + composite (`and`, `or`, `not`) + membership (`in`, `not_in`).
- `state.X` paths resolve through the v0.1.2 state-persist helpers.
- 28 new smoke assertions (`tests/eval_expr_cond_smoke.c`).

## v0.1.2 — 2026-05-26

### Added — M2.1 (per-execution state file)
- `Pollen.StateGet(rootMid, path) → string` and `Pollen.StateSet(rootMid, path, jsonLiteral) → bool`.
- Atomic tmp + rename writes under `<sharedDir>/state/<rootMid>.json`.
- `Pollen.WorkflowSetSharedDir(path)` gains best-effort `mkdir`.
- Smoke test harness wired into CI (`tests/run_tests.sh`).
- 7 smoke assertions.

## v0.1.1 — 2026-05-26

### Fixed
- `amalgame.toml` was missing the `[package]` section — `amc package add` rejected the manifest. Linkable + resolvable after this fix.

## v0.1.0 — 2026-05-26

### Initial release
- Package skeleton with linkable stubs for the full target API surface.
- CI workflow building the package archive against `amc v0.8.52`.
- Stubs log `amalgame-pollen v0.1.0-dev : <Method> is a stub` to stderr when their real behaviour is missing.

### Known issue (fixed in v0.1.1)
- Missing `[package]` section in `amalgame.toml` — `amc package add` rejects this version.

---

## Roadmap

- **M2.4** — debug bridge : manager `:3001` phone-home protocol, `DEBUG_PAUSE` injection, step / continue / cancel / mutate cycles.
- **M3** — refactor the `pollen` CLI binary into a ~80-LOC thin wrapper around this package.
- **M4** — refactor `pollen-manager` to call `Pollen.Publish` directly instead of going through TCP.
- **v0.2** — multi-engine support (one process can host two independent workflows ; engine state behind an opaque handle instead of static globals).
