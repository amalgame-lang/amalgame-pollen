# NOTICE — Pollen

## Authorship

Copyright 2026 Bastien Mouget. Part of the
[Amalgame](https://github.com/amalgame-lang/Amalgame) ecosystem.
External contributions are paused at the ecosystem level; see the
main repo's `CONTRIBUTING.md` for the policy.

AI tools (Anthropic Claude) were used during development. Per the
project's authorship policy, AI is treated as a tool, not a
co-author at law.

## Wire-protocol reference

Pollen's message envelopes (MESSAGE / ACK, `messageId` +
`rootMessageId`, versioned `topic`, JSON `data`) come from the
Node.js reference implementation **TARMeule** by the same author
([github.com/BastienMOUGET/TARMeule](https://github.com/BastienMOUGET/TARMeule)).
Pollen v0.1 was a direct port of its UDP transport (datagrams, ACK
retry, UUID dedup, optional AES-256-CBC).

Since the 2026-05-25 rewrite, Pollen runs over TCP instead : one
newline-delimited JSON envelope per line, ACKed on the same
connection, connections reused per peer since v0.5. UDP lost
datagrams under load (socket buffer drops, retries, p99 latency
around 1.4 s) where TCP stays at a few milliseconds. Encryption is
not part of the TCP transport yet. A Pollen node and a TARMeule node
no longer talk to each other directly.

## Licence

Apache License 2.0. See `LICENSE` for the full text.
