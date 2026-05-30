# Changelog

## v0.3.0 — 2026-05-30

### Internal — deep cleanup of dead v2 C helpers (Phase 6 follow-up)

v0.2.0 retired the v2 public API but left the v2 internals (~2000
LOC of unreachable C in `facade.am`'s `@c {}` block). v0.3.0 finishes
the job — drops all helpers that were only callable from the
removed wrappers.

Removed from `facade.am` (no behaviour change — all of this was
already unreachable from AM after v0.2.0) :

- All orphan `_c` wrappers : `WorkflowLoad_c`, `StateGet/StateSet_c`,
  `EvalExpr/EvalCond_c`, `OnMessage/OnComplete/Forward_c`,
  `PublishDebug_c`, `WorkflowVersion_c`, plus the 16 M2.3c.x setter
  wrappers (`WorkflowReloadBegin/Commit/AddConsume/AddNext/SetEmitTopic_c`,
  `CondBranchOpen/AddTarget/SetTopic_c`, `SetOpAdd_c`, `ForSetup/
  AddTarget/AddItem/SetTopic_c`, `WhileSetup/AddExit/SetExitTopic/
  SetBodyTopic_c`).
- Mosaic bridge globals + helper : `_pollen_on_message`,
  `_pollen_on_complete`, `_pollen_replace_data`.
- Debug bridge (M2.4) : `_pollen_debug_process`, `_pollen_debug_should_pause`,
  `_pollen_debug_extract_manager`, `_pollen_debug_rewrite`,
  `_pollen_debug_rewrite_data`, `_pollen_debug_pause` (~330 LOC).
- M2.3c.x dispatch helpers : `_pollen_wf_forward_one`,
  `_pollen_wf_forward_all`, `_pollen_wf_rebuild_envelope`,
  `_pollen_wf_topic_consumed`, `_pollen_wf_reload_begin/commit`,
  `_pollen_wf_add_consume/add_next/set_emit_topic`, plus the cond
  branch setters (`_pollen_cond_*`), set-op setters
  (`_pollen_set_ops_*`), for/while setters + forwarders
  (`_pollen_for_*`, `_pollen_while_*`) — ~600 LOC.
- M2.2 expression + cond evaluator (`_pollen_eval_expr`,
  `_pollen_cond_eval_*`, `_pollen_resolve_path`,
  `_pollen_expr_val_*`, …) — ~580 LOC.
- Phase 5.3 per-execution state file I/O (`_pollen_state_filepath`,
  `_pollen_state_dir_ensure`, `_pollen_state_read/write/find_value/set`,
  `_pollen_extract_root_mid`) — ~170 LOC.
- M2.3c.1 workflow runtime state : `_pollen_wf_active`, `_pollen_wf_n_consumes`,
  `_pollen_wf_n_nexts`, `_pollen_wf_consumes[]`, `_pollen_wf_next_host[]`,
  `_pollen_wf_next_port[]`, `_pollen_wf_emit_topic`, `_pollen_wf_version`,
  plus the cond/set-op/for/while storage structs — ~125 LOC.

### Carried changes

- The capability writer now advertises this node's `actions` from
  the v3 entries' `on:` topics (was the v2 consume list). New helper
  `_pollen_v3_format_consumed_topics` snapshots them under the
  wf mutex.
- `_pollen_json_field` (generic JSON field finder) moved next to
  `_pollen_find` so it's available to the capability registry
  parser without depending on the dropped M2.2 block.
- Header (`runtime/Amalgame_Pollen.h`) gets a forward declaration
  of `Amalgame_Formats_Json_JsonValue` so `WorkflowV3DispatchEntryState`
  / `DispatchTopicState` return types match the consumer's
  amc-emitted TU (previously declared as `void*`, which conflicted
  at link time).

### Net diff vs v0.2.0

- `facade.am` 6960 → 4790 LOC (-2170, ~31% smaller)
- generated `.c` 9570 → 7483 lines (-2087)
- package archive 167 KB → ~163 KB

No behaviour change, no API surface change. All 198 test
assertions still green (cel_lite, v3_validator, v3_loader,
v3_dispatch, v3_listener, header_consumer_check, listener_smoke,
publish_smoke).

## v0.2.0 — 2026-05-30

### Breaking — v1/v2 dispatcher dropped (Phase 6)

The v2 workflow surface is gone. Workflows now describe themselves
with the v3 schema (`{schema: "pollen/v3", actions, entries}`) and
dispatch happens through `PollenDispatcher` (tree-walking interpreter,
CEL-lite expressions, capability-registry-resolved `call` steps with
`on_error` policies).

Removed from the public API :

- `WorkflowLoad`, `WorkflowVersion` (v2 loader)
- `WorkflowReloadBegin` / `Commit` / `AddConsume` / `AddNext` /
  `SetEmitTopic` (v2 routing setters)
- `CondBranchOpen` / `AddTarget` / `SetTopic` (v2 cond routing)
- `SetOpAdd` (v2 set-op storage)
- `ForSetup` / `AddTarget` / `AddItem` / `SetTopic` (v2 for routing)
- `WhileSetup` / `AddExit` / `SetExitTopic` / `SetBodyTopic` (v2 while routing)
- `StateGet` / `StateSet` (v2 per-execution state file)
- `EvalExpr` / `EvalCond` (v2 expression evaluator — superseded by CEL-lite)
- `PublishDebug` + the M2.4 debug bridge (`mode=step` / `mode=breakpoint`,
  manager `:3001` phone-home, mutate-on-pause workflow). The v3-shaped
  equivalent is unscheduled.
- `OnMessage` / `OnComplete` / `Forward` Mosaic bridge — was wired into
  the v2 listener path only. A v3 bridge (per-step callbacks on the
  AM-side `PollenDispatcher`) is unscheduled.

Retained — every shared infrastructure piece both v3 and any future
bridge will keep using : `WorkflowSetSharedDir` / `WorkflowSetSelf` /
`WorkflowActiveRole`, `StartListener`, `StartCapabilityWriter` /
`StartCapabilityReader` / `SetLoadBalance` / `RegistrySize` /
`ResolveProvider`, `Publish` / `PublishSync`.

Dropped artefacts :

- 6 `examples/workflow-v2-*.json` fixtures (superseded by `workflow-v3-*.json`)
- 10 v2-only C-side smoke tests (`cond_set_dispatch_smoke`,
  `eval_expr_cond_smoke`, `for_while_smoke`, `state_persist_smoke`,
  `workflow_dispatch_smoke`, `debug_bridge_smoke`, `publish_debug_smoke`,
  `bridge_smoke`, `lb_smoke`, `recorder_smoke`)

Simplified `_pollen_listener_worker` : the v3 fork (Phase 3d) is now
the only dispatch path. v2 routing tables + `do_forward` machinery +
the entire `if (do_forward) { … }` block (debug bridge, OnMessage
transform, set-ops apply, while/for/cond/forward-all chain, v2
recorder hop, OnComplete leaf-call) are gone. ~140 LOC out of the
listener body alone.

Runtime header (`runtime/Amalgame_Pollen.h`) rewritten to declare the
v3 surface only — 39 public symbols (down from ~38 v2 + some v3 stubs).
`header_consumer_check.c` rewritten to match.

Net diff : `facade.am` 7376 → 6960 LOC (-416), generated `.c` 10010 →
9570 lines (-440). Tests: 13 C-side → 4 (`header_consumer_check`,
`listener_smoke`, `publish_smoke`, `v3_listener_smoke`) + the 4
AM-side v3 suites.

### Deferred to v0.3.0

The v2 C-side helpers (`_pollen_wf_*` workflow state, `_pollen_cond_*`
eval, `_pollen_for_*` / `_pollen_while_*` setters + dispatch,
`_pollen_set_ops_*`, `_pollen_debug_*` bridge, `_pollen_state_*` file
I/O) are still defined inside `facade.am`'s `@c {}` block. They're
unreachable from the public API now but bloat the compiled archive.
Pruning them is mechanical but voluminous (~2000 LOC across many
sections, with `static` storage scattered through the early TU) — kept
as a v0.3 cleanup pass so v0.2.0 can ship the API contract change
without coupling it to a deep refactor.

### Added — Pollen v3 Live executions recording + cycle test (Phase 4 partial)

Spec : `docs/proposals/pollen-v3.md` §"Implementation phases" Phase 4.

Two robustness items that were carried as Phase 3d follow-ups :

- **Live executions recorder hooked into v3** — after Phase 3d, v3
  dispatch hops were invisible to the pollen-manager Live executions
  panel because `_pollen_wf_record_step` was only fired on the v2
  path. The listener worker now emits one record per v3-routed
  message : fresh hop UUID, parent = inbound mid, topic_in = matched
  topic, n_nexts=1, optional debug-session passthrough. Same shape
  as the v2 record so the manager renders v2 + v3 hops uniformly.
- **Negative test for cycle detection (rule 6)** — the validator's
  DFS strongly-connected-components walk over the goto graph was
  shipped in the rule 1-7 base but never exercised by a green-build
  fixture. `examples/workflow-v3-cycle-invalid.json` (2 entries
  pointing at each other) + a new `RunNegative` helper in
  `tests/v3_validator_smoke.am` assert that the validator emits an
  `[error rule6]` for the cycle. Catches a regression class
  (silently-passing cycles) that would only surface at runtime
  otherwise.

### Notes

- Runtime depth-cap fuzz (POLLEN_CALL_STACK_MAX=64) would need a
  65-entry goto chain — verbose for the value, documented as a v4
  candidate.
- The v2 → v3 migration tool (~300 LOC per the spec) is still pending,
  required before existing workflows can move over.

### Added — Pollen v3 listener → dispatcher integration (Phase 3d)

Spec : `docs/proposals/pollen-v3.md` §"v3 dispatcher (target)".

`_pollen_listener_worker` (the v2 hot path) is patched to fork to
v3 when a v3 entry consumes the inbound message's topic. The v2
routing tables remain the fallback for any topic no v3 entry
claims, so existing v2 deployments are unaffected.

- Forward decls at the top of the listener worker for
  `_pollen_v3_active` / `_pollen_v3_entry_count` /
  `_pollen_v3_dispatch_count` (tentative C definitions, fused with
  the Phase 2 storage definitions) and the AM-generated
  `Amalgame_Pollen_Pollen_WorkflowV3DispatchTopic` entry point.
- New `v3_handled` flag in the worker's per-message decision block,
  set when `_pollen_v3_lookup_entry_by_topic` finds a match. The
  envelope + topic are GC-duped while still holding the wf mutex so
  the deferred AM call has stable storage.
- The AM dispatcher is invoked AFTER releasing `_pollen_wf_mutex`.
  Doing the call inside the critical section would risk deadlock —
  the dispatcher allocates GC objects and may call Pollen.Publish
  which re-locks the registry.
- New `_pollen_v3_dispatch_count` atomic counter, bumped each time
  the worker routes to v3. Exposed via
  `Pollen.WorkflowV3DispatchCount` and
  `Pollen.WorkflowV3ResetDispatchCount` so tests can observe the
  fork without needing to inspect per-dispatch state (which is
  otherwise ephemeral per `PollenDispatcher.Run`).

### Tests

- `tests/v3_listener_smoke.c` — C-side integration test
  (registered via the existing `tests/run_tests.sh` runner so CI
  picks it up automatically). Spins the listener on an ephemeral
  port in a pthread, loads the v3 fixture, publishes to
  "tick.hourly" then to "no.such.topic" via the real `Publish`
  TCP path, polls `WorkflowV3DispatchCount`, and asserts :
  - dispatch count = 1 after the v3-consumed publish
  - dispatch count = 1 (unchanged) after the unrelated publish

7 assertions, all green via `./tests/run_tests.sh ~/.local/bin/amc`.

### Notes

- The v2 hot path is preserved : every message still walks through
  the same `_pollen_listener_worker` ; v3 simply takes precedence
  when its lookup matches.
- The Phase 2 executions recorder (`_pollen_wf_record_step` for the
  manager's Live executions panel) is NOT yet hooked up for v3 —
  follow-up before v0.2.0 ships.

### Added — Pollen v3 bus-triggered dispatch (Phase 3c)

Spec : `docs/proposals/pollen-v3.md` §"v3 dispatcher (target)".

Exposes the topic→entry routing surface the listener thread will
call once Phase 3d patches `_pollen_listener_worker`. Keeping the
listener change out of this commit because it touches a hot path
that's currently exercised by every v2 message — wants its own
slice with dedicated wire-level integration tests.

- C-side `_pollen_v3_lookup_entry_by_topic(topic)` — linear scan
  over `_pollen_v3_entries[]` matching `on_topic`. Bounded at 256
  entries (`POLLEN_MAX_ENTRIES`), sub-µs even fully loaded — same
  pattern as `_pollen_wf_topic_consumed` in the v2 path.
- AM-side wrappers :
  - `Pollen.WorkflowV3LookupEntryByTopic(topic) → int`
  - `Pollen.WorkflowV3DispatchTopic(topic, envelopeJson) → int`
  - `Pollen.WorkflowV3DispatchTopicState(topic, envelopeJson) → JsonValue`
- The dispatcher's `BuildEnv` already binds the parsed envelope as
  `msg`, so CEL-lite paths like `msg.data.user.id` and
  `msg.data.n * 2` resolve out of the box — no code change needed,
  just exercised by the new fixture entry.

### Tests

Added `by-topic` entry to the fixture (with `on: tick.hourly` and
three `set` steps that read msg.data fields). 5 new assertions :
lookup hit, lookup miss, three msg.data-driven `set` results.
35 dispatcher assertions total, all green.

### Notes

- Phase 3d will patch `_pollen_listener_worker`'s
  `if (matched) { do_forward = 1; }` block : when `_pollen_v3_active`
  and the topic matches a v3 entry, call
  `Amalgame_Pollen_Pollen_WorkflowV3DispatchTopic(topic, envelope)`
  instead of the v2 routing-table forward. v2 stays the fallback for
  topics that no v3 entry consumes.
- Phase 3d will also need an integration smoke test that spins up a
  real listener + posts an envelope + asserts the dispatcher ran.

### Added — Pollen v3 `call` step wired to the capability registry (Phase 3b)

Spec : `docs/proposals/pollen-v3.md` §"`call` resolution".

The Phase 3a `call` stub is replaced by real capability-registry
dispatch reusing the v2 LB + publish primitives.

- New C helpers in the top `@c` block :
  - `_pollen_v3_action_topic(name)` — action → topic lookup
  - `_pollen_v3_publish_all(topic, data)` — registry-iterate +
    `_pollen_publish_one` per provider declaring the topic
  - `_pollen_v3_call_action(action, mode, on_error, data)` —
    top-level entry. mode=0 (one) → `_pollen_lb_pick` P2C ; mode=1
    (all) → broadcast. Returns providers-published-to, or -1 when
    `on_error="fail"` finds 0 providers.
- `PollenDispatcher.StepCall` rewritten : audits the call into
  `state.__calls[]` (audit list stays for tests), JSON-encodes the
  state via `Json.Encode`, calls the C helper. -1 return → `Fail()`
  aborts the dispatch.
- `on_error` policies fully enforced :
  - `"log"`  *(default)* — 0 providers warns + chain continues
  - `"fail"` — 0 providers aborts (the trailing step does NOT run)
  - `"drop"` — 0 providers silently continues

### Tests

`tests/v3_dispatch_smoke.am` extended with three new entries in
`workflow-v3-dispatch-fixture.json` (`with-call-fail`,
`with-call-drop`, `with-call-all`). 30 assertions total ; verifies :
- fail policy : `rc=-1`, `before` ran, `after` did NOT, audit captures
  the attempted call
- drop policy : full chain runs silently
- mode=all : audit records the call ; 0-provider case returns 0 sent

### Notes

- The listener-thread wiring (incoming bus message → dispatch via
  v3 when `_pollen_v3_active`) is deferred to Phase 3c. Today the v3
  dispatcher is only callable via `Pollen.WorkflowV3DispatchEntry`.
- The C-side `_pollen_v3_publish_all` snapshots the registry under
  the mutex, then sends outside the lock — same pattern as the v2
  fan-out forwarder, keeps slow socket I/O off the registry hot path.

### Added — Pollen v3 tree-walker dispatcher (Phase 3a, AM-side)

Spec : `docs/proposals/pollen-v3.md` §"v3 dispatcher".

Choice : AM-side dispatcher (not C). Reuses the Phase 1 CelEval verbatim
for every expression — no parallel C-side eval implementation. Reads
the AST through the Phase 2 C-side accessors.

- `PollenFrame` — one call-stack frame : entry idx, param bindings,
  bind-key (where to write the entry's `returns` on pop), caller-idx.
- `PollenDispatcher` — owns the frame stack (cap 64,
  `POLLEN_CALL_STACK_MAX`), the per-message state blackboard
  (`JsonValue` object), and the bus envelope. Public entry points :
  - `Pollen.WorkflowV3DispatchEntry(eidx, envelopeJson) → int`
  - `Pollen.WorkflowV3DispatchEntryState(eidx, envelopeJson) → JsonValue`
  - dotted-key state Set/Get walking nested objects, creating leaves
    on demand.
- Step handlers implemented :
  - `set`    — eval value, write to dotted state key
  - `goto`   — push frame, bind params from `args[]`, dispatch target
    body, eval `returns`, pop, bind into caller's state via `bind`
  - `anchor` — fall-through no-op
  - `if`     — first-match-wins over `cases[].when` + `else: true`
  - `for`    — eval `in`, iterate the list, bind loop var on the
    current frame's params (also exposed at the env root so bare
    `item.X` works per cel-lite §4)
  - `while`  — eval `cond`, dispatch body, bounded by `maxIter` or
    a 100k hard cap
  - `call`   — STUB this slice : records the action name into
    `state.__calls[]` so tests can verify the dispatcher walked it.
    Full LB via capability registry + bus forwarding ships in
    Phase 3b.
- Eval bridges : `BuildEnv()` exposes `state` / `params` / `msg` plus
  each frame param as a top-level root ; `CelToJson` / `JsonToCel`
  bridge the two value types.
- 16 additional C-side accessors needed by the dispatcher exposed via
  AM wrappers (NodeExpr / NodeBind / NodeMode / NodeOnError /
  NodeMaxIter / NodeHasElse / NodeArgsCount / NodeArg /
  EntryHasReturns / EntryReturns / EntryParamCount / EntryParam /
  AnchorEntryIdx / AnchorNodeIdx).

### Tests

- `tests/v3_dispatch_smoke.am` (+ `build-v3-dispatch-smoke.sh`) +
  `examples/workflow-v3-dispatch-fixture.json` — 21 assertions
  covering every implemented step (linear sequence, arithmetic, if
  branching, for iteration + sum, while + maxIter, goto with params /
  returns / bind, call stub audit). All green.

### Notes

- v2 dispatcher / routing tables remain alive. This phase adds a
  parallel v3 dispatch entry point ; no wiring into the bus listener
  yet (Phase 3b).
- `call` is intentionally a stub here — once the listener fork lands
  it'll dispatch through the capability registry with mode=one (P2C
  LB) / mode=all (broadcast) per the spec.

### Added — Pollen v3 runtime loader + AST + name resolver (Phase 2, C-side)

Spec : `docs/proposals/pollen-v3.md` §"v3 dispatcher", §"Memory bounds".

- C-side static buffers in the top-level `@c` block:
  - `_pollen_v3_ast[4096]` (`POLLEN_MAX_AST_NODES`) — flat AST pool,
    indexed by int32. Each node carries `kind`, two child slots,
    `next_sibling` for sequence chains, `goto_kind`/`goto_idx` for
    resolved targets, and GC-allocated `name` / `expr` / `bind_key`.
  - `_pollen_v3_entries[256]` (`POLLEN_MAX_ENTRIES`) — entries pool
    with `name`, `on_topic`, `do_root`, `returns_expr`, `params[8]`.
  - `_pollen_v3_anchors[4096]` (`POLLEN_MAX_ANCHORS_TOTAL`).
  - `_pollen_v3_actions[256]` (`POLLEN_MAX_ACTIONS`).
  - `_pollen_v3_args_pool[1024]` — flat pool for `goto.args[]`.
- Setter API: `_pollen_v3_reload_begin` → 18 setters → `_pollen_v3_reload_commit`.
  The commit step runs `_pollen_v3_resolve_gotos`, which walks the
  AST and links every `goto` node to its target entry / anchor idx.
- Introspection API: 11 `_pollen_v3_count_*` / `_pollen_v3_node_*`
  / `_pollen_v3_entry_*` accessors for tests + dispatcher debug.

- AM-side `Pollen.WorkflowLoadV3(path) → bool` walks the JSON via
  `JsonParser` and drives the C setters. Mirrors the v2 reload
  pattern (Begin → setters → Commit) — Phase 3's dispatcher will
  consume the populated buffers directly.

- AM-side introspection wrappers : `WorkflowV3IsActive` /
  `WorkflowV3EntryCount` / `WorkflowV3AnchorCount` / `WorkflowV3NodeCount` /
  `WorkflowV3EntryName(eidx)` / `WorkflowV3EntryDoRoot(eidx)` /
  `WorkflowV3NodeKind(idx)` / `WorkflowV3NodeChild0(idx)` /
  `WorkflowV3NodeNext(idx)` / `WorkflowV3NodeGotoKind(idx)` /
  `WorkflowV3NodeGotoIdx(idx)` / `WorkflowV3NodeName(idx)` /
  `WorkflowV3LoadErrorMsg`.

### Tests

- `tests/v3_loader_smoke.am` (+ `build-v3-loader-smoke.sh`) — loads
  every v3 example, asserts entries / anchors / actions counts, then
  walks the feature-demo AST to verify the structural shape
  (sibling chain + child0 nesting + goto resolution against both
  entry and anchor targets). 67 assertions, all green.

### Notes

- v2 dispatcher / routing tables stay alive in parallel — this phase
  is loader-only, no execution yet. Phase 3 wires the tree-walker.
- Goto resolution happens at commit time ; unresolved targets are
  flagged in the load-error trail so the dispatcher (Phase 3) can
  fail-fast even if a workflow slips through the validator.

### Added — Pollen v3 CEL-lite expression engine (Phase 1, AM-side)

Spec : `docs/proposals/pollen-v3-cel-lite.md`. Lives in `facade.am`.

- `CelLexer`  — char-by-char tokenizer, every literal/op from the spec
  (ints, floats, strings with `\" \\ \n \t` escapes, bools, null,
  idents, all single/two-char operators). Bound : 256 tokens.
- `CelParser` — Pratt parser with a precedence table for binary ops,
  explicit dispatch for unary/postfix/ternary/primary, list literals
  and calls. Bound : 128 AST nodes. Produces an indexed node pool
  consumed by the evaluator.
- `CelValue` — tagged-union runtime value (`Null/Bool/Int/Float/Str/List_`).
- `CelEnv`   — path-root resolver bridge, JSON-backed. Caller pushes
  the well-known roots (`state`, `params`, `msg`) plus any for-loop
  var binding before eval ; missing path → `null` per spec.
- `CelEval`  — tree-walking evaluator implementing every coercion +
  builtin (`len`, `int`, `float`, `string`, `bool`, `contains`,
  `startsWith`, `endsWith`), short-circuit `&&` / `||`, ternary,
  `in` for `string in string` and `T in list<T>`, list indexing,
  div/mod-by-zero → null, ordered compare against null → type error.

### Validator rule 8 now fully enforced

- New private helper `Pollen._v3CheckExpr(expr, ctx, path, diags)` runs
  Lex + Parse on every expression string and emits `[error rule8]`
  diagnostics with lex/parse error messages + column numbers.
- All previous "non-empty" stub checks for `set.value`, `if.cases[].when`,
  `for.in`, `while.cond`, `goto.args[]`, `entry.returns` replaced by
  the new helper. The 9 shipped v3 example workflows still validate
  with zero errors.

### Tests

- `tests/cel_lite_smoke.am` (+ `build-cel-lite-smoke.sh`) — 83 assertions
  covering parse happy / error path, arithmetic, comparison, logical
  ops, string concat + builtins, `in` operator, list indexing, and
  env-backed path resolution. All green.
- Existing `tests/v3_validator_smoke.am` — still 9/9 clean.

### Notes
- The unknown-path-root warn from cel-lite.md §4 is **deferred**. The
  validator can't distinguish a typo from a legit for-loop var without
  threading scope info ; the Phase 2 runtime resolver will fire the
  warning instead.
- Locked-out v0.4+ features (comprehensions, macros, regex, map
  literals, string slicing, bitwise ops) reject cleanly at parse time.


## v0.1.23 — 2026-05-28

### Added — explicit `while.body` (uniform schema with `for.do` / `if.then`)
- `Pollen.WhileSetBodyTopic(topic)` — set the body action's consume topic. When set (+ LB on), the controller forwards each iteration to a registry-resolved provider of that topic (the body action) instead of looping to self. The body action processes one iteration and forwards back to the controller (its emit topic = the controller's consume topic), which re-evaluates the cond + bumps iter, and so on. Empty → v1 fallback (loop to self).
- 37 public symbols.

### Why
- `if { branches[].then }`, `for { do }`, `while { … }` all expressed bodies differently — `while` was the outlier (no `body` field, implicit self-loop). v2 schema gets uniform : `while { cond, maxIter, body }`. Mirrors n8n / Temporal-style flow editors and makes `while` representable in a DAG editor without special-casing.

### Reference node wiring
- `RunV2` `while` branch : if `while.body` is set, calls `WhileSetBodyTopic(actionTopic(body.action))`.
- A role that is the body of some `while` (detected by walking the tree) has its emit topic set to the controller's consume topic, so the body's forward returns to the controller.
- New helpers : `FindWhileController(treeV, role)`.

### Notes
- v1 path (`while` without `body`, looping to self via `loop_host:loop_port`) still works untouched.
- v0.1.22's `WhileSetExitTopic` is unchanged.


## v0.1.22 — 2026-05-27

### Added — v2 `while` routing (registry-resolved exit)
- `Pollen.WhileSetExitTopic(topic)` — set the `while` route's EXIT topic. When set (+ LB on), on exit the envelope is rebuilt with that topic and forwarded to a live provider resolved by power-of-two — no static exit targets. The **loop body** still uses `selfHost:selfPort` (passed to `WhileSetup`) so iteration stays on this node deterministically (looping through the registry would bounce between replicas of the loop role).
- 36 public symbols, 156 assertions.

### Significance
- **v2 runtime is now complete** : linear `call` (forward_all + LB) + `if` (cond branches by topic, v0.1.20) + `for` (per-item fan-out by topic, v0.1.21) + `while` (loop self + exit by topic, v0.1.22). The reference node `RunV2` wires all four. v1 (`nodes{}` + static targets) still works in parallel.
- Next : manager editor v2 — render `actions{}` in the DAG + outline + author v2 trees from the UI.


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
