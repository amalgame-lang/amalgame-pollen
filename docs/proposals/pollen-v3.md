# Pollen v3 — schema + runtime + UI roadmap

**Status**: proposal — not yet implemented.
**Author**: Bastien (vision) + Claude (drafting).
**Date**: 2026-05-29.
**Target**: amalgame-pollen v0.2.0 + pollen-manager refactor.

## Why v3

The v2 runtime (`workflow-tree/v2`, shipped in amalgame-pollen
v0.1.5 → v0.1.8) was built around a **routing-table dispatcher**:
the workflow tree is parsed once at load time, then flattened into
static `_pollen_for_route` / `_pollen_while_route` / `_pollen_if_route`
tables. Forwarding is a network fan-out via
`_pollen_wf_forward_one(host, port, …)`.

This design **couples control flow to network topology**:
- A `for` loop is fundamentally "fan-out to N targets"
- A `while` loop is "self-loop to one target until cond fails"
- An `if` is "first matching target wins"

Bastien's vision is the opposite: **control flow is local, network
forwarding is one primitive among others**. A `for` should be able
to wrap an `if` should be able to wrap a `while` should be able to
wrap a `call` — at any depth — without the runtime distinguishing
"depth 1 fan-out is OK, depth 2 fan-out is not".

The known bug **`for → if` jamais exécuté** is a direct symptom of
the routing-table model: when the loader walks `for.do` and sees
an `if`, it has nowhere to put it in the route tables. The `if` is
silently dropped from the routing layer.

The same root cause produces other latent bugs:
- `while.do = for` — likely broken
- `if.cases[i].do = while` — likely broken
- Any 3+ level nesting — undefined behavior

The fix is **not** to extend the routing tables to handle nesting
(that's the lowering approach, still bug-prone). It's to **replace
the dispatcher with a tree walker interpreter**.

## The vision (Bastien's words, 2026-05-29 session)

> *"for c'est un bouclage et on peut mettre ce que l'on veux dedans
> call fan_out if ...., on peut foucler en fonction des données ou
> constante ou tout ce que l'on connais dans les données qui transitent.
> il n'y a que for (pas de for do ou autres for quelques chose) du coup
> puisse que l'on peut faire ce que l'on veux partout à n'importe quelle
> profondeur. while suit la meme regle mais en while. autant de
> profondeur et de type que l'on veux !!!"*

> *"autant d'entrée que l'on veux et sortie aussi et des workflow qui
> ne se melangent jamais c'est possibble aussi..."*

> *"on peut meme appelé fan_out call aussi, mais un call peut avoir un
> ou plusieurs serveur dispo pour le LB, de toute facon l'info est partagé
> sur le directory partagé."*

> *"sequence ?! ca sert à quoi ?
> une sortie ca peut etre aussi un appel à une API full rest ou autre
> chose c'est pas grave, meme pas besoin de exit, le dernier noeud fait
> son taf et c'est tout."*

> *"dans le workflow on peut avoir plusieurs sous workflow, genre goto
> et aprés on reviens au goto une fois l'autre workflow terminé."*

> *"chaque entries dans le workflow est un onglet. il faut juste un bloc
> start avec un nom pour démarrer ou on veut le call sub."*

> *"biensur on peut avoir plusieurs entrée … donc on peut avoir un start
> nommé et un appel API aussi par exemple donc deux fleche qui arrivent
> au meme point."*

Distilled into design principles:

1. **Uniform containers**. `for` / `while` / `if` take an arbitrary
   sub-step (or array of sub-steps) as body. No constraint on what
   that body can be. Any depth.
2. **One primitive: `call`**. There is no `fan_out` type. A `call`
   resolves its target(s) through the capability registry; whether
   one provider or many serve the action is an orchestration concern,
   not a primitive concern.
3. **Unified `entries[]`**. Every entry has a `name`. An optional
   `on:` topic makes it bus-triggered; absent, it's callable-only
   via `goto`. Same shape, different invocation paths.
4. **Multiple exits, implicit**. The last step of a branch is the
   exit. No `exit` primitive needed.
5. **Disconnected sub-workflows**. Entries that don't share topics
   never interact — naturally supported, no syntax change.
6. **No explicit sequence type**. An array of steps IS a sequence.
   `do` can be either a single step or an array.
7. **Goto + anchors**. A `goto` step calls any entry by name OR
   any anchor by name. Anchors are leaf labels placed inside a body,
   reachable only via `goto` (never via bus trigger). This buys
   "resume-at-mid-body" without adding a second execution model.

## Schema `pollen/v3`

### Top level

```json
{
  "schema": "pollen/v3",
  "name": "string — human label",
  "version": 1,

  "actions": {
    "<name>": { "topic": "string — Pollen topic this action consumes" }
  },

  "entries": [
    {
      "name": "string — entry label, unique per workflow",
      "on":      "string?    — Pollen topic that triggers this entry",
      "params":  ["string?", ...],
      "returns": "Expr?      — value returned to caller when invoked via goto",
      "do":      Step | Step[]
    },
    ...
  ]
}
```

Notes:
- `entries[]` is **flat top-level**. No `subworkflows[]` array.
- Each entry has a unique `name` within the workflow.
- `on:` is **optional** — present → bus-triggered, absent → goto-only.
- `params[]` declares positional bindings consumed by `goto args`.
  Legal on every entry (bus-triggered + goto-callable both work).
- `returns` declares the value returned when invoked via `goto`;
  ignored when triggered via bus.
- A given entry can be **dual-mode** (both `on:` AND targeted by
  `goto`). Validator warns to confirm intent.

### Step types

```ts
type Step =
  | { type: "call";   action: string; mode?: "one" | "all" }
  | { type: "if";     cases: Case[] }
  | { type: "for";    var: string; in: Expr; do: Step | Step[] }
  | { type: "while";  cond: Expr; maxIter?: number; do: Step | Step[] }
  | { type: "set";    key: string; value: Expr }
  | { type: "goto";   target: string; args?: Expr[]; bind?: string }
  | { type: "anchor"; name: string }

type Case =
  | { when: Expr;     do: Step | Step[] }
  | { else: true;     do: Step | Step[] }   // optional, last case

type Expr = string                          // dotted state path, JSON literal, or template
```

Step-type notes:
- **`do: Step | Step[]`** — single step or array. Array semantics:
  run in order, each step starts after the previous returns.
- **No `type: "sequence"`** — an array IS the sequence.
- **No `type: "exit"`** — the last step in a branch is the exit.
- **`call.mode`** defaults to `"one"` (LB pick one provider via
  power-of-two-choices from the capability registry). `"all"`
  broadcasts to every provider currently advertising the action.
- **`if.cases[]`** — first match wins. The optional terminal
  `{else: true, do}` matches if no `when` did.
- **`for.in`** — an `Expr` evaluating to an array of items, OR
  a JSON-array literal `[1,2,3]`, OR a state path like
  `"state.items"`.
- **`goto`**:
  - `target` resolves into the **flat global namespace** of
    entry-names and anchor-names within the same workflow.
  - `args[]` (optional) are evaluated and bound to the target's
    `params[]` in order. Missing args bind to `null`.
  - `bind` (optional) names a state key that captures the target's
    `returns` value when control comes back.
- **`anchor`** is a leaf no-op when fall-through reaches it during
  normal execution. Only `goto` targets it. **No `on:`** allowed.

### Example — every feature

```json
{
  "schema": "pollen/v3",
  "name": "v3-feature-demo",
  "version": 1,
  "actions": {
    "fetch":  { "topic": "data.fetch" },
    "store":  { "topic": "data.store" },
    "alert":  { "topic": "ops.alert" }
  },
  "entries": [
    {
      "name": "via-cron",
      "on":   "tick",
      "do":   { "type": "goto", "target": "shared-pipeline" }
    },
    {
      "name": "via-http",
      "on":   "http.compute.request",
      "do":   { "type": "goto", "target": "shared-pipeline" }
    },
    {
      "name": "shared-pipeline",
      "params": ["dataset"],
      "returns": "state.summary",
      "do": [
        { "type": "call", "action": "fetch" },
        { "type": "anchor", "name": "after-fetch" },
        { "type": "for", "var": "item", "in": "state.items",
          "do": {
            "type": "if",
            "cases": [
              { "when": "item.priority > 5",
                "do":   { "type": "call", "action": "alert" } },
              { "else": true,
                "do":   { "type": "call", "action": "store" } }
            ] } },
        { "type": "set", "key": "state.summary", "value": "state.items.length" }
      ]
    },
    {
      "name": "retry-from-cache",
      "params": ["dataset"],
      "do":     { "type": "goto", "target": "after-fetch" }
    }
  ]
}
```

This exercises **every** new feature:
- Multi-entry convergence (`via-cron` + `via-http` → `shared-pipeline`)
- Anchor reachable only via goto (`after-fetch`)
- `retry-from-cache` skips the fetch by jumping straight to the
  `after-fetch` anchor (mid-body resume)
- Nested control-flow (for→if→call)
- Callable-only entry (`shared-pipeline`, `retry-from-cache` — no `on:`)
- Dual-mode bus entries (`via-cron`, `via-http` — bus + callable-via-goto)
- Returns / args (`shared-pipeline` declares both)

### What disappears from v2

| v2 | v3 replacement |
|----|----------------|
| `tree` (single root) | `entries[]` (multiple roots, callable + triggered uniformly) |
| `type: "sequence"` + `steps[]` | `do: Step[]` (array IS the sequence) |
| `type: "fan_out"` | `type: "call"` with `mode: "all"` |
| `if.then` / `if.else` | `if.cases[]` with `when` / `else: true` |
| `for.do` (still named that, semantic change) | same name, body is now ANY step |
| `while.body` / `while.do` (was inconsistent) | unified `do` |

### Validator rules

The v3 validator enforces:

1. **Unique names**. Each entry name + each anchor name must be
   unique within the workflow. Anchor-name collision with entry-name
   is an error (single flat namespace for `goto target`).
2. **Reachability**. Every entry with no `on:` AND no `goto`
   targeting its name → **error** ("unreachable entry").
3. **Anchor reachability**. Every anchor with no `goto` targeting
   its name → **warn** ("orphan anchor").
4. **Dual-mode warn**. An entry with `on:` that is ALSO targeted by
   a `goto` → **warn** ("dual-mode entry, ensure params binding is
   intentional — bus envelope vs goto args").
5. **Return discard**. An entry with `returns` that has `on:` and
   is never `goto`-targeted → **warn** ("return value will be
   discarded by bus dispatcher").
6. **Cycle detection**. Statically walk the goto call-graph; any
   strongly-connected component (recursion) is **rejected at load**
   unless explicitly opted in via a per-workflow
   `"allow_recursion": true` flag (not in v0.2.0 — future feature).
7. **Depth bound**. The dispatcher's call-stack is capped at
   ~64 frames at runtime; the validator additionally warns if a
   static call-chain exceeds 32 (heuristic for "you might want to
   refactor").

## Runtime architecture — interpreter, not router

### v2 dispatcher (current — to be replaced)

```
Workflow JSON
    ↓ LoadWorkflow
Routing tables (_pollen_for_route, _pollen_while_route, _pollen_if_route)
    ↓ on message received
Match topic against route ← (static lookup)
    ↓
_pollen_wf_forward_one(target_host, target_port, …) ← (network send)
```

Pros: O(1) dispatch. Cons: depth-1 by construction.

### v3 dispatcher (target)

```
Workflow JSON
    ↓ LoadWorkflow
AST per entry + Map<name, NodePath> for entries and anchors
    ↓ on bus message (topic → entry) OR on goto step (target → entry/anchor)
PUSH call-stack frame {
    cursor:   pointer into AST,
    locals:   bound params,
    return:   caller frame + bind key
}
DispatchStep(step, ctx) ← recursive interpreter
    ├─ call    → resolve via registry, send to N targets
    ├─ if      → evaluate cases until match, DispatchStep(case.do, ctx)
    ├─ for     → for each item: bind var, DispatchStep(do, ctx')
    ├─ while   → while cond AND iter<maxIter: DispatchStep(do, ctx')
    ├─ set     → mutate state.X
    ├─ goto    → PUSH frame, jump to target's NodePath
    ├─ anchor  → no-op on fall-through (just step cursor past it)
    └─ array   → for step in array: DispatchStep(step, ctx)
ON natural return: POP frame, bind `returns` to caller's `bind` key, resume caller cursor
```

`ctx` carries the current message envelope + state + iteration
binding. Each step that creates a sub-context (for, while) clones
the parent + adds its binding.

### Call-stack details

- Each `goto` pushes a frame: `{cursor, locals, return_to: caller_frame, bind_key}`.
- Depth capped at **64 frames** (configurable via `POLLEN_CALL_STACK_MAX`).
  Overflow → runtime error `"recursion depth exceeded in subworkflow X"`
  with the full goto-chain logged.
- Static cycle detection at validation time prevents trivial
  recursion (A→A or A→B→A) from ever reaching runtime.

### State scoping

- `state.X` reads / writes are **shared across the call-stack
  frames of one bus-message lifetime** (a single bus trigger fires
  one chain; all gotos within it see the same `state.X`).
- Cross-bus-message persistence goes through the shared dir
  (`sharedDir/state/`) explicitly, same as v2.
- `params[]` of an entry are **frame-local** — they don't pollute
  `state.X`, only the current frame's bindings.

### `call` resolution

```
call.action = "store"
   ↓
Lookup actions[].topic → "data.store"
   ↓
Capability registry: who consumes "data.store"?
   ├─ 0 providers → drop + log warning (TODO: error mode?)
   ├─ 1 provider  → send to that one
   ├─ N providers, mode=="one" → power-of-two-choices pick → send to one
   └─ N providers, mode=="all" → send to all
```

The registry lookup is **per call invocation**, not per workflow
load. A provider that comes up mid-run becomes discoverable on the
next call, without restarting the workflow.

## UI architecture — flowchart with arrows, not block-nested

### Pivot from earlier draft

The first draft proposed Slice A's block-nested renderer (Scratch /
Blockly style, enclosing containers for for / while / if). After
discussion 2026-05-29, the visual pivots to a **structured flowchart
with explicit arrows**: each step is a box, sequences are vertical
arrows between boxes, loops are loop-back arrows, branches are
diverging arrows that converge after.

Why: containment shows STRUCTURE; arrows show FLOW. Pollen users
need to see WHERE THE EXECUTION GOES — block-nested hides the
loop-back and branch-converge under conventions instead of drawing
them honestly.

### Visual mapping

| v3 construct | Visual |
|---|---|
| `entries[]` | Multiple "Start" badges floating in the canvas, each with the entry name + (if present) `on: <topic>` bus icon, arrow pointing to the entry's first step |
| `do: [a, b, c]` (array) | Vertical sequence of boxes connected by arrows |
| `do: {step}` (single) | One box |
| `call action` | Atomic blue box. Badge "1 of N providers" when registry shows multiple providers |
| `call mode: "all"` | Blue box, badge "broadcast → N" |
| `if cases[]` | Diamond / header box with one branch per `case`, branches diverge, all converge at a single point below |
| `for var in <expr>` | Header box, arrow into the body, body executes, loop-back arrow returns to the header. Exit arrow when iteration ends |
| `while cond` | Same as for, header shows `cond`, badge `maxIter` if set |
| `set key = value` | Atomic yellow-green box |
| `goto target` | Arrow that physically connects the goto-site to the target. Long-distance arrows render as dashed lines to distinguish "near sibling flow" from "non-local jump" |
| `anchor name` | Small flag icon on the body. Tooltip lists every `goto` that targets it, so inbound edges are visible at a glance |
| Multi-exit | Implicit. A branch with no following step has no outgoing arrow |
| Convergence of N entries → 1 target | N start badges, all with arrows ending at the same target box |

### Layout

**Always-on dagre** (no auto-routing button). The layout is a pure
function of the AST + the resolved goto-graph. There is no
"manual position" field anywhere in the schema or the on-disk state.

- Library: dagre (~30 KB, supports back-edges and branch-converge).
- Re-runs on every edit (animated transition).
- Long-distance goto arrows: dagre by default routes them with bends;
  we override to dash-style + endpoint highlight to distinguish from
  local sibling-arrows.

### Sub-workflow navigation (tabs)

**Tabs at the top of the canvas, one per entry.** The entry name
is the tab label. A small bus icon (📡 or similar) on tabs whose
entry has `on:`; flag icon (🚩) on tabs that are pure-callable.

- Active tab shows the entry's AST as a flowchart.
- A goto step in the canvas is clickable: clicking jumps to the
  target's tab + scrolls to the target step.
- Breadcrumb above the canvas shows the goto call-stack when the
  manager is in step-by-step debug mode: `via-cron → shared-pipeline → after-fetch`.

When the number of entries grows beyond ~10-15, the tab bar may
get crowded. **Mitigation deferred to v4**:
- Search/filter entries
- Group tabs into folders
- Switch to sidebar tree view as an opt-in
For v3 release, accept the tab-bar growth and document the
"refactor into multiple workflow files" pattern as the scaling story.

### Folding

At depth 5+, even a flowchart-with-arrows can get visually dense.
Mitigation: **fold/unfold containers**. Click a `for` / `while` /
`if` header to collapse its body into a single placeholder box
labeled `[+15 steps inside]`. Click again to expand.

State of fold/unfold is stored in localStorage per workflow file,
keyed by path.

## Implementation phases

### Phase 1 — Schema v3 spec + validator + examples
**Time**: 2-3 days

- [ ] Lock the v3 schema (this doc).
- [ ] Write a v3 validator in AM (`facade.am` — `WorkflowValidateV3`).
- [ ] Convert all v2 examples in `examples/` to v3 equivalents.
- [ ] Write a v2 → v3 migration script.
- [ ] Static cycle detection across `goto` call-graph.

### Phase 2 — v3 loader + AST + name resolver in runtime
**Time**: 3-5 days

- [ ] Parse v3 JSON into an AST tree per entry.
- [ ] Build a `Map<name, NodePath>` covering entries + anchors.
- [ ] Store the AST in C-level static buffers (no malloc per dispatch).
- [ ] Hook into existing `WorkflowReloadBegin` / `WorkflowReloadCommit`
      cycle — add `WorkflowReloadBeginV3`.
- [ ] Keep v2 loader alive for one release (dual-mode based on
      `schema` field), drop in v0.3.0.

### Phase 3 — Tree walker dispatcher + call-stack
**Time**: 1 week

- [ ] Replace routing tables with a single `DispatchStep(step, ctx)`
      recursive interpreter.
- [ ] Implement step handlers: `call`, `if`, `for`, `while`, `set`,
      `goto`, `anchor`, array (sequence).
- [ ] Implement call-stack with depth cap (~64) and runtime overflow
      error.
- [ ] Migrate `call` to use registry-resolved targets.
- [ ] State scoping: shared per bus-message lifetime, params frame-local.

### Phase 4 — Tests rewrite
**Time**: 1 week (parallelizable with Phase 3)

- [ ] Drop `for_while_smoke.c` (or move to `tests/legacy_v2/`).
- [ ] Write `v3_dispatch_smoke.c` exercising nested control-flow
      (for→if→while→call), multi-entry, dual-mode entries, anchors,
      mid-body resume via goto, `call.mode: all`.
- [ ] Re-port the 78 overnight assertions to v3.
- [ ] Add tests for depth cap + cycle rejection.
- [ ] Add fuzz tests for AST depth (verify no stack overflow at
      depth 10, 50, 100).

### Phase 5 — pollen-manager UI
**Time**: 4-5 days

- [ ] Replace Slice A block-nested renderer with flowchart-with-arrows
      via dagre.
- [ ] Tabs at top, one per entry, with bus / flag icons.
- [ ] Clickable goto: navigate to target tab + scroll to target step.
- [ ] Breadcrumb call-stack display in debug mode.
- [ ] Anchor flag icon + tooltip showing inbound `goto` callers.
- [ ] LB badge on `call` blocks: lookup registry, show "1 of N
      providers" when N > 1.
- [ ] v2 → v3 migration UI: detect `schema: "workflow-tree/v2"` on
      load, offer to convert + save back.
- [ ] Tree outline tab kept as fallback (per prior workflow recommendation
      — tabs DAG | Tree, persisted in localStorage).
- [ ] Fold / unfold containers, persisted in localStorage.

### Phase 6 — Cleanup + release
**Time**: 2-3 days

- [ ] Drop v2 dispatcher code paths (kept dual through Phase 5).
- [ ] Update `README.md`, `CHANGELOG.md`.
- [ ] Tag `amalgame-pollen v0.2.0`.
- [ ] Register in packages-index.

**Total**: ~4 weeks of focused work.

## Open questions to settle before Phase 2

1. **State scoping precise semantics**. Shared across one
   bus-message lifetime is the proposal. Confirmed? Or do we want
   per-goto-frame state copies that merge on return?
2. **Error handling on `call` 0-providers**. Drop silently (today),
   drop + log, fail the message-chain explicitly? Probably needs a
   per-workflow `on_error` policy.
3. **Recursion opt-in**. v0.2.0 rejects all recursion at validation.
   Should we allow opt-in via `"allow_recursion": true` for advanced
   users? Or defer to v4?
4. **Expression language**. Today `cond` strings use a custom walker
   (`_pollen_cond_eval_expr` at `facade.am:618`). Sufficient for v3?
   Or upgrade to a small embedded DSL (e.g. CEL-lite)?
5. **Binary persistence**. The AST + call-stack + name-resolver
   buffers — what's the upper bound? `POLLEN_MAX_*` constants need
   updating.
6. **Tabs scaling beyond 10-15 entries**. Search? Folders? Sidebar
   tree opt-in? Or just document "split into multiple files"?

## Compatibility

- **Schema**: v2 files keep loading under a `--legacy-v2` mode until
  v0.3.0. v3 is the default for new files.
- **Runtime**: dual dispatcher during Phase 2-3; v2 path frozen
  (no new features, only bug fixes if critical).
- **Manager**: handles both schemas; converts v2 on load with an
  in-UI prompt; saves only v3.
- **Tests**: v2 smoke tests moved to `tests/legacy_v2/`, marked as
  `skipped` after the v0.2.0 release.

## Out of scope (v4+)

- **Drag-to-edit canvas** (drop blocks from a palette, wire by
  mouse). Currently the Tree outline + JSON file are the editing
  surfaces; the DAG is read-only. Was prior Slice C, deferred.
- **n8n-style free-form edges** carrying data between arbitrary
  steps. Block-with-arrows is honest about Pollen's tree semantics;
  free-form edges would mislead users into thinking edges carry
  data, which Pollen doesn't model.
- **Inline entries with `on:` triggers** (entries placed mid-body
  that fire on bus messages). Considered + rejected 2026-05-29 —
  bus subscriber that resumes mid-body is a debugging horror.
  Anchors (goto-only, no `on:`) are the chosen compromise.
- **DSL for `Expr`** beyond what v2 already supports. Deferred until
  real users hit the wall of the current cond walker.
- **Hot-reload of workflow files**. Today a reload requires the
  `WorkflowReloadBegin` / `Commit` cycle; making this transparent
  (file-watch + atomic swap) is a v4 concern.
- **Manual canvas layout** (drag boxes around). The schema and on-disk
  state intentionally have no `position` field; layout is always
  computed client-side by dagre.

## Links

- v2 implementation: `facade.am:~1300-2400` (dispatch logic), `~1900-2000` (loader).
- Capability registry (used by `call` LB): Phase 6.3, shipped v0.1.8.
- pollen-manager Slice A renderer (to be replaced by flowchart):
  `public/manager.js:renderBlockDag`.
- dagre layout library: https://github.com/dagrejs/dagre
- Prior session memory: `roadmap_acme_autorenew_timer.md`,
  `project_pollen_manager_dag_refactor.md`.
