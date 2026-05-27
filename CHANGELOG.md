# Changelog

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
