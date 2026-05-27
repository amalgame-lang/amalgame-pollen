# Changelog

All notable changes to `amalgame-pollen`. Format inspired by
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) ;
versioning follows the upstream package indexing convention.

## v0.1.10 — 2026-05-27

### Fixed
- `runtime/Amalgame_Pollen.h` included a stray `"Amalgame.h"` (no such file in the amc runtime dir), which broke **any consumer** whose generated C pulls in the header — `fatal error: Amalgame.h: No such file`. The package's own `--lib` build never tripped it because `facade.c` includes `_runtime.h` directly. Now includes `"_runtime.h"` + `<stdint.h>` like every other package header. This unblocks building a program that does `import Amalgame.Pollen` (e.g. `examples/pollen-node.am`).

### Added
- `examples/pollen-node.am` — reference Pollen node binary built entirely on the package (the M3 "thin wrapper"). Parses a `workflow.json`, resolves the role, wires the runtime via the public setters (consumes / emit / next / if-branches), and blocks in `Pollen.StartListener`. Unlike the legacy binary, the `if` cond is evaluated correctly at dispatch.

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
