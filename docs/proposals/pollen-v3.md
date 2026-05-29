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
   strongly-connected component (recursion) is **rejected at load**.
   No opt-in flag in v0.2.0 — recursion is deferred to v4 entirely
   (decision locked 2026-05-29).
7. **Depth bound**. The dispatcher's call-stack is capped at
   ~64 frames at runtime; the validator additionally warns if a
   static call-chain exceeds 32 (heuristic for "you might want to
   refactor").
8. **CEL-lite syntax**. Every `cond` / `value` / `in` expression
   string must parse cleanly under the CEL-lite grammar (see
   "Expression language" below). Parse errors are **rejected at
   load** with the source span pointed at.

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

### Memory bounds (locked 2026-05-29)

Fixed-size static buffers, no per-dispatch malloc. Overrun → clear
load-time error with the offending workflow + count. Each bound is
overridable via env var for advanced users / stress tests.

| Constant | Default | Env override |
|---|---|---|
| `POLLEN_MAX_ENTRIES` | 256 per workflow file | `POLLEN_MAX_ENTRIES` |
| `POLLEN_MAX_ANCHORS_PER_ENTRY` | 64 | `POLLEN_MAX_ANCHORS_PER_ENTRY` |
| `POLLEN_MAX_AST_NODES` | 4096 per workflow file | `POLLEN_MAX_AST_NODES` |
| `POLLEN_CALL_STACK_MAX` | 64 frames | `POLLEN_CALL_STACK_MAX` |
| `POLLEN_MAX_CEL_TOKENS` | 256 per expression | `POLLEN_MAX_CEL_TOKENS` |
| `POLLEN_MAX_CEL_AST_NODES` | 128 per expression | `POLLEN_MAX_CEL_AST_NODES` |

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
   ├─ 0 providers → on_error policy (see below)
   ├─ 1 provider  → send to that one
   ├─ N providers, mode=="one" → power-of-two-choices pick → send to one
   └─ N providers, mode=="all" → send to all
```

The registry lookup is **per call invocation**, not per workflow
load. A provider that comes up mid-run becomes discoverable on the
next call, without restarting the workflow.

#### `on_error` policy on `call` (locked 2026-05-29)

Each `call` step accepts an optional `on_error` field:

| Value | Behavior on 0 providers |
|---|---|
| `"log"` *(default)* | Emit a warning to the bus log, continue the message-chain past this step (the step is treated as if completed with no return value). |
| `"fail"` | Abort the current message-chain immediately, emit an error log with the goto-chain context. The caller frame's `bind` key is left unset. |
| `"drop"` | Silently skip the step (no log, no abort). For best-effort fan-out where missing providers are expected. |

For `mode: "all"` with 0 providers, the same policy applies (a
broadcast to nobody is treated identically to a unicast that can't
resolve). For `mode: "all"` with N>0 providers where some deliveries
fail at the transport layer, that's a separate "delivery retry"
concern (out of scope for v0.2).

## Expression language — CEL-lite (locked 2026-05-29)

v2 used `_pollen_cond_eval_expr` (`facade.am:618`), a small custom
walker, for conditions in `if.cases[].when`, `while.cond`,
`for.in`, and `set.value`. v3 upgrades to a documented mini-DSL —
**CEL-lite** — so expressions are validatable at load time, errors
are pointable, and the language surface is teachable.

CEL-lite is a strict subset of Google's CEL (Common Expression
Language). The chosen subset is intentionally small to fit a hand-
written lexer + Pratt parser + tree-walking evaluator in ~600
lines of AM, and to keep the AST under `POLLEN_MAX_CEL_AST_NODES`.

### Supported types

- `int` (i64), `float` (f64), `bool`, `string`
- `list<T>` (homogeneous via runtime check), `null`
- Path access: `state.X`, `params.X`, `msg.data.X`, dotted chains
  on json-shaped maps (e.g. `msg.data.user.id`)

### Supported operators

| Class | Operators | Notes |
|---|---|---|
| Arithmetic | `+ - * / %` | int+int → int, otherwise float |
| Comparison | `== != < <= > >=` | strict, no implicit conversion across types |
| Logical | `&& \|\| !` | short-circuit |
| String | `+` (concat), `in` (substring + list membership) | |
| List | `in`, `len(x)`, `x[i]` | indexed read only, no write |
| Conditional | `cond ? a : b` | both branches type-checked |
| Grouping | `( )` | |

Precedence follows CEL (same as C). All operators are left-assoc
except `?:` (right-assoc).

### Supported literals + calls

- Literals: `42`, `3.14`, `"hello"`, `true`, `false`, `null`, `[1,2,3]`
- Builtins: `len(s)`, `len(list)`, `int(x)`, `float(x)`, `string(x)`,
  `bool(x)`, `contains(s, sub)`, `startsWith(s, p)`, `endsWith(s, p)`
- **No** user-defined functions, no closures, no comprehensions, no
  macros (defer to v4 if requested)

### Examples

```
// while.cond — loop until queue is drained
state.queue.len > 0 && state.tries < 5

// if.cases[].when — branch on a JSON field
msg.data.kind == "user" && msg.data.user.role in ["admin", "owner"]

// for.in — iterate a list expression
state.candidates

// set.value — compute a new field
"user-" + string(msg.data.id) + "-" + state.suffix
```

### Error handling at eval time

- Type mismatch (e.g. `int + string`) → step fails with `on_error`
  policy (defaults to `"log"` and continues; future: per-step
  override). The chain is logged with the source span pointed at.
- Path miss (`state.X` where `X` undefined) → evaluates to `null`,
  consistent with JSON dotted access. `null` comparison with `==`
  /`!=` is allowed; with `<` etc. it's a type error.
- Division by zero → returns `null` (not abort).

### Validator integration

The validator parses every expression string at load time. Parse
errors are **load-time rejected** (rule 8). Type-checking happens
at eval time (since `msg.data.X` is unknown statically), but the
validator does emit warnings for trivially-typed mismatches
(e.g. `"a" + 1`).

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

### Sub-workflow navigation (list sidebar, locked 2026-05-29)

**Pivot from the initial tabs proposal.** Tabs imitate a "files open
in an IDE" mental model, but an entry isn't a file context — it's a
node in the workflow, often related to others (goto inbound, called-by).
The list shape scales to 50+ entries with search + fold and lets us
show rich per-entry metadata permanently (badge `📡` for bus-triggered,
`🚩` for callable-only, `called by N`, `dead` warning). Tab-bars
truncate to a label and scroll horizontally past 10–12 entries.

**Left sidebar** lists every entry, grouped:

```
┌─────────────────────────┐
│ [Search…]               │
├─────────────────────────┤
│ 📡 Bus-triggered     ▼  │
│   ▸ via-cron         3  │
│   ▸ via-http-hook    1  │
├─────────────────────────┤
│ 🚩 Callable          ▼  │
│   ▸ shared-pipeline 12  │
│   ▸ after-fetch      5  │
├─────────────────────────┤
│ ⚠ Dead (unreachable) ▶  │
└─────────────────────────┘
```

Per-row metadata:
- Badge `📡` if the entry has `on:`, `🚩` if pure-callable.
- Right-aligned number = inbound `goto` count.
- Italic + `⚠` if the entry is unreachable (validator rule 2).
- Dot indicator if the entry is currently being debugged (Phase 5 debug mode).

Anchors fold under their parent entry as nested rows (depth-1):
```
▸ shared-pipeline       12
    🚩 after-fetch       5
    🚩 cleanup           2
```

Group headers (`📡 Bus-triggered`, `🚩 Callable`, `⚠ Dead`) are
foldable. Search box filters by entry/anchor name with substring
match. State persisted in localStorage per workflow file.

- Selecting a sidebar row swaps the canvas to that entry's flowchart.
- A `goto` step in the canvas is clickable: clicking selects the
  target's sidebar row + scrolls the canvas to the target step.
- Breadcrumb above the canvas shows the goto call-stack when the
  manager is in step-by-step debug mode: `via-cron → shared-pipeline → after-fetch`.

No schema change required — sidebar UI is a pure function of the
loaded workflow (entries[] + anchor names + goto-graph + validator
output). When the number of entries grows past ~50 in a single file,
the docs recommend splitting into multiple workflow files (each file
remains a self-contained unit; cross-file goto is **not** supported
in v0.2 — kept as an open question for v4).

### Folding

At depth 5+, even a flowchart-with-arrows can get visually dense.
Mitigation: **fold/unfold containers**. Click a `for` / `while` /
`if` header to collapse its body into a single placeholder box
labeled `[+15 steps inside]`. Click again to expand.

State of fold/unfold is stored in localStorage per workflow file,
keyed by path.

## Implementation phases

### Phase 1 — Schema v3 spec + validator + CEL-lite + examples
**Time**: 4-6 days *(grown from 2-3d: CEL-lite added per Q4 decision)*

- [x] Lock the v3 schema (this doc).
- [ ] CEL-lite lexer + Pratt parser + tree-walking evaluator in AM
      (`src/v3/cel.am`), under `POLLEN_MAX_CEL_*` bounds.
- [ ] Write a v3 validator in AM (`facade.am` — `WorkflowValidateV3`),
      enforcing rules 1–8 incl. CEL-lite parse + static cycle detection
      (DFS strongly-connected-components on the goto call-graph).
- [ ] Convert all v2 examples in `examples/` to v3 equivalents (target
      5–10 covering: nested for/while/if, multi-entry, dual-mode,
      anchors, mid-body resume via goto, `call.mode: all`, `on_error`).
- [ ] Write a v2 → v3 migration script (preserves semantics, emits
      goto/anchor as needed, fails loudly on patterns it can't translate).

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

## Resolved decisions (2026-05-29 session)

The six open questions are locked. Each links to the section that
bakes the choice into the spec.

| # | Question | Decision | Where in this doc |
|---|---|---|---|
| 1 | State scoping with gotos | **Shared bus-message lifetime.** `state.X` is a per-message blackboard visible across all call-stack frames; `params[]` is frame-local. | "State scoping" |
| 2 | `call` 0-providers behavior | **Drop + log by default**, per-step `on_error: "log" \| "fail" \| "drop"` override. No workflow-level policy. | "`call` resolution" → "`on_error` policy" |
| 3 | Recursion opt-in | **Defer to v4 entirely.** v0.2 rejects all cycles at load (DFS SCC on goto call-graph). No `allow_recursion` flag. | Validator rule 6 |
| 4 | Expression language | **CEL-lite.** Hand-written lexer + Pratt parser + tree-walking evaluator AM-side. Strict CEL subset, ~600 LOC target. Documented grammar + supported types + builtins. | "Expression language — CEL-lite" |
| 5 | Memory bounds | **Fixed-size static buffers**, default constants tunable via env var. entries=256, anchors/entry=64, AST=4096, callstack=64, CEL tokens=256, CEL AST=128. | "Memory bounds" |
| 6 | UI tabs scaling | **Pivot from tabs to list sidebar (left).** Grouped by trigger type, foldable, with rich metadata (badges, called-by counts). Scales to 50+; recommend file split beyond. | "Sub-workflow navigation (list sidebar)" |

These six were the gating items for Phase 2. Phase 1 scope grew from
2-3d to 4-6d to accommodate the CEL-lite lexer/parser/eval — see
Implementation phases.

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
