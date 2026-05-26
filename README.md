# amalgame-pollen

> Workflow engine for [Pollen](https://github.com/amalgame-lang/pollen) — runs `workflow.json` v2 trees over a P2P TCP message bus.

This package extracts the workflow runtime that originally lived inside the `pollen` CLI binary, so it can be embedded :

- by the `pollen` CLI itself (thin wrapper)
- by Mosaic web apps that want to trigger workflows from HTTP routes
- by any Amalgame program that needs the `if`/`for`/`while`/`set` tree engine

## Status

Phase **v0.1.0-dev** — skeleton + public facade declared. Implementation migration from `pollen/tools/pollen.am` (formerly `pollen-node-tcp.am`) in progress.

| Milestone | Scope | Status |
|---|---|---|
| M1 | Package skeleton (toml + header + facade.am stubs) | done |
| M2 | Migrate state_persist + expr_eval into facade.am | pending |
| M3 | Migrate tree_eval + tcp_transport + debug_bridge | pending |
| M4 | Refactor `pollen` CLI to import this package | pending |
| M5 | Refactor `pollen-manager` to call `Pollen.Publish` directly (no TCP indirection) | pending |
| v0.2 | Multi-engine (one process can host two independent workflows) | future |

## Usage (target API)

```amalgame
import Amalgame.Pollen

// Embed in a Mosaic web app : HTTP POST triggers a workflow.
public class Page {
    public static HttpResponse POST(ctx: WebContext) {
        let mid: string = Pollen.Publish("127.0.0.1", 8000,
                                          "order.in", 1, ctx.Body)
        return HttpResponse.New()
            .Header("content-type", "application/json")
            .Text("{\"messageId\":\"" + mid + "\"}")
    }
}
```

## Limitations (v0.1)

- One Pollen engine per process — engine state is in static C globals (matches `pollen` v0.2 behavior). v0.2 will optionally wrap in an opaque handle.
- Single-source class while amc's multi-source package story stabilises ; sibling .am modules to be split out later.

## License

Apache-2.0. See [LICENSE](LICENSE).
