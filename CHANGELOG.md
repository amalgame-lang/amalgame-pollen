# Changelog

All notable changes to `amalgame-pollen`. Format inspired by
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) ;
versioning follows the upstream package indexing convention.

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
