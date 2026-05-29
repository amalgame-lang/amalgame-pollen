# CEL-lite — Pollen v3 expression language spec

**Status**: draft for review (2026-05-29). Locked once Bastien signs off.
Parent: [pollen-v3.md](./pollen-v3.md), Q4 decision.

CEL-lite is the expression language used in every `cond` / `value` /
`in` field of a v3 workflow. It's a strict subset of Google's
[CEL](https://github.com/google/cel-spec), chosen to fit in a hand-
written lexer + Pratt parser + tree-walking evaluator (~600 LOC AM)
with static memory bounds and no dependencies.

This doc is the implementation contract. Anything not listed here is
**not** supported in v0.2 and the parser must reject it at load time.

---

## 1. Lexical grammar

```
ident      = letter (letter | digit | "_")*
letter     = "a"..."z" | "A"..."Z" | "_"
digit      = "0"..."9"
int        = digit+                                      // i64
float      = digit+ "." digit+                           // f64
string     = '"' (any-char-except-" | '\"' | "\\" | "\n" | "\t")* '"'
bool       = "true" | "false"
null       = "null"
punct      = "(" | ")" | "[" | "]" | "," | "." | "?" | ":"
op         = "+" | "-" | "*" | "/" | "%"
           | "==" | "!=" | "<" | "<=" | ">" | ">="
           | "&&" | "||" | "!" | "in"
ws         = (" " | "\t" | "\n" | "\r")+                 // skipped
```

- `ident` cannot be a keyword (`true`, `false`, `null`, `in`).
- Strings only support `\"`, `\\`, `\n`, `\t`. No `\u`, no `\x`,
  no multiline. Keeps the lexer tiny.
- Integer overflow during lexing → parse error.
- Max token count per expression: `POLLEN_MAX_CEL_TOKENS` (default 256).

## 2. Grammar (EBNF)

```ebnf
expr        = ternary

ternary     = logic_or [ "?" expr ":" expr ]

logic_or    = logic_and { "||" logic_and }
logic_and   = equality  { "&&" equality }

equality    = comparison { ("==" | "!=") comparison }
comparison  = additive { ("<" | "<=" | ">" | ">=" | "in") additive }

additive    = multiplicative { ("+" | "-") multiplicative }
multiplicative = unary { ("*" | "/" | "%") unary }

unary       = ("!" | "-") unary
            | postfix

postfix     = primary { "." ident | "[" expr "]" }

primary     = int | float | string | bool | null
            | ident [ "(" [ expr { "," expr } ] ")" ]   // call OR plain ident
            | "(" expr ")"
            | "[" [ expr { "," expr } ] "]"             // list literal
```

## 3. Precedence + associativity

Lowest precedence first. Mirrors C / CEL.

| Level | Operators | Associativity |
|---|---|---|
| 1 | `? :` | right |
| 2 | `\|\|` | left |
| 3 | `&&` | left |
| 4 | `==` `!=` | left |
| 5 | `<` `<=` `>` `>=` `in` | left |
| 6 | `+` `-` | left |
| 7 | `*` `/` `%` | left |
| 8 | unary `!` `-` | right |
| 9 | postfix `.` `[]` `()` | left |

Implemented as a Pratt parser (single function with a precedence
table). No recursive descent per operator class.

## 4. Type system

### Types

| Kind | Notation | Notes |
|---|---|---|
| `int`     | `i64`   | Lex as digits with no dot |
| `float`   | `f64`   | Lex as digits with a dot |
| `bool`    | `code_bool` | `true` / `false` only |
| `string`  | `code_string` | UTF-8 byte sequence |
| `list<T>` | `[a, b, c]` literal or via path | Homogeneous at construction; mixed types → runtime type error on op |
| `null`    | `null`  | Singleton, returned by missing paths and `/0` |

There is **no map type as a literal**. Paths like `state.user.role`
walk into JSON-shaped objects already loaded at runtime; the
intermediate node is a runtime value, not an expressible type.

### Coercion rules

- Arithmetic `+ - * / %`:
  - `int op int` → `int` (overflow = wrap, same as C; rationale:
    cond expressions don't bench-math)
  - any operand is `float` → both coerced to `float`, result `float`
  - `string + string` → string concat (only `+`)
  - any other combo → **type error** at eval (`on_error` policy)
- Comparison `== != < <= > >=`:
  - Same-type only. `int == int`, `float == float`, `string == string`,
    `bool == bool`. `int == float` → coerce to float, then compare.
  - `null == null` is `true`; `null == anything-else` is `false`.
  - `null != X` is the negation.
  - Ordered compare with `null` on either side → **type error**.
- Logical `&& || !`:
  - Operands must be `bool`. Non-bool → **type error**.
  - Short-circuit: `false && X` skips X; `true || X` skips X.
- `in` operator:
  - `string in string` → substring check (i.e. `"a" in "cab"` is `true`)
  - `T in list<T>` → element membership, `==`-based
  - Other combos → **type error**.
- Index `[]`:
  - `list[int]` only. Out-of-range → `null`. Negative index → type error.
  - `string[int]` is **not** supported in v0.2 (use `contains` builtin).
- Ternary `?:`:
  - Condition must be `bool`. Branches may differ in type; the result
    type is the union (caller must handle).

### Path access (`.` chain)

- Roots: `state`, `params`, `msg`. Anything else as an ident at the
  root of a path → parse error (caught at validation, rule 8).
- `state.X` reads the bus-message-lifetime blackboard; `params.X`
  reads the current frame's bound params; `msg.data.X` walks the
  envelope.
- Missing intermediate key → the whole path evaluates to `null`,
  no error (JSON dotted convention).
- A path can have at most 8 segments after the root (configurable).

## 5. Builtins

| Signature | Semantics |
|---|---|
| `len(s: string) -> int`  | UTF-8 byte length |
| `len(l: list<T>) -> int` | Element count |
| `int(x) -> int`          | `string → int` via strtol; `float → int` via trunc; `bool → 0\|1`; `null → 0` |
| `float(x) -> float`      | `string → float` via strtod; `int → float`; others → type error |
| `string(x) -> string`    | Canonical representation of any value; `null → ""`, `bool → "true"\|"false"` |
| `bool(x) -> bool`        | `string` non-empty / `int!=0` / `float!=0` / `null → false` |
| `contains(s, sub) -> bool`   | Substring check (same as `sub in s`) |
| `startsWith(s, p) -> bool`   | Prefix check |
| `endsWith(s, p) -> bool`     | Suffix check |

**Locked**: no other builtins in v0.2. `lower()`, `upper()`,
`trim()`, `split()`, `replace()`, `regex(*)`, math ops (`abs`,
`min`, `max`, `floor`, `ceil`) → v4. Custom user functions → v4.

## 6. Error model

| Class | When | Behavior |
|---|---|---|
| Lex error | Unterminated string, unknown char, integer overflow at lex | Validator rule 8 rejects workflow at load |
| Parse error | Unexpected token, missing `)`, unknown ident at path root | Validator rule 8 rejects workflow at load |
| Bound error | Token count > `POLLEN_MAX_CEL_TOKENS` or AST nodes > `POLLEN_MAX_CEL_AST_NODES` | Validator rejects |
| Type error at eval | `int + string`, `null < 5`, etc. | Step's `on_error` policy fires (default `"log"`) |
| Path miss | `state.X` where X undefined | Evaluates to `null` silently |
| Div-by-zero | `x / 0`, `x % 0` | Returns `null` (no abort) |

Eval errors carry a `{kind, span, msg}` triple that's surfaced to
the step's `on_error` log line: `eval failed at cond@col=12: int + string`.

## 7. Memory bounds

All buffers are static, sized for the worst case from the constants
in [pollen-v3.md#memory-bounds](./pollen-v3.md). The evaluator does
zero malloc per step.

| Buffer | Size | Constant |
|---|---|---|
| Token array per expression | 256 tokens × ~32 bytes = 8 KB | `POLLEN_MAX_CEL_TOKENS` |
| AST node pool per expression | 128 nodes × ~40 bytes = 5 KB | `POLLEN_MAX_CEL_AST_NODES` |
| Value stack at eval | depth 32, ~48 B/value = 1.5 KB | hard-coded |
| String scratch | 4 KB (for `string(x)` + concat) | hard-coded |

One expression buffer is reused across all expressions in a workflow
(re-lexed/re-parsed per evaluation — Phase 3 may cache parsed ASTs
if perf demands it). Total static cost ≈ 18 KB. Negligible.

## 8. Examples

**Conditions**:
```
state.queue.len > 0 && state.tries < 5

msg.data.kind == "user" && msg.data.role in ["admin", "owner"]

(state.retries == null ? 0 : state.retries) < 3

contains(msg.data.url, "amalgame.me")
```

**Set values**:
```
"user-" + string(msg.data.id) + "-" + state.suffix

state.count == null ? 1 : state.count + 1

int(msg.data.id) * 2
```

**For-in iterables**:
```
state.candidates

msg.data.items
```

## 9. Out of scope (v4+)

- Comprehensions: `[x for x in list if cond]`
- Macros: `has(x.y.z)`, `all(list, lambda)`, `exists(list, lambda)`
- User-defined functions
- Regex builtins
- Map literals: `{key: value}`
- String slicing: `s[1:3]`
- Bitwise ops
- Date/time builtins

These are recognised CEL features but each adds non-trivial parser
+ eval surface. We document them as "v4 candidates" so users have
a clear signal.

## 10. Implementation plan

Single file (or section of `facade.am`), three blocks:

1. **Lexer** (~150 LOC): char-by-char tokenizer, produces token array
   in the static buffer. Handles all literal forms + ops + idents.
2. **Pratt parser** (~200 LOC): one `parse_expr(min_precedence)`
   function with a precedence-and-action table. Allocates AST nodes
   from the static pool.
3. **Tree-walking evaluator** (~250 LOC): `eval_node(env, node) →
   value`. Operates on a tagged-union value type. Implements all
   ops + builtins + coercion rules from sections 4-5.

Bound by the `POLLEN_MAX_CEL_*` constants; over-budget = clear
error before runtime.

## 11. Open questions (this doc)

1. **String interpolation** (`"hello {{name}}"`)? Currently no.
   v2 has it via the cond walker for `set.value`. Decision: keep
   it **out** of CEL-lite. Users compose with `+` and `string()`.
   *Confirm Bastien?*
2. **Strict equality on `null`?** Currently `null == null → true`,
   `null == 0 → false`. CEL spec says the same. *Confirm OK?*
3. **String escapes**: only `\"`, `\\`, `\n`, `\t` proposed. CEL
   has `\u{XXXX}` Unicode escapes. Decision: defer to v4 (UTF-8
   strings can be embedded directly). *Confirm OK?*
