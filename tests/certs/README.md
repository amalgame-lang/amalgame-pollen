# Test certificates — do not use outside the tests

PKI for `tests/tls_smoke.am`, generated with OpenSSL (EC P-256, valid
100 years). The private keys are public on purpose : these files only
exist so the test can run anywhere. `make-rights.sh` re-issues the node
certificates with their rights (v0.8.0).

| File | Signed by | Rights and claims (subjectAltName URIs) | Role in the test |
|---|---|---|---|
| `ca.pem` / `ca.key` | itself | | the "site" CA |
| `node-a.*` | `ca` | `pollen:publish:tls.*`, `test:site:alpha` | the test process (listener) |
| `node-b.*` | `ca` | `pollen:publish:tls.test`, `test:site:beta`, `test:role:edge` | a legitimate second node |
| `node-c.*` | `ca` | `pollen:publish:other.*`, `test:site:gamma` | right CA, wrong topic : refused |
| `node-none.*` | `ca` | none | right CA, no right : refused |
| `rogue-ca.*` | itself | | another CA |
| `intruder.*` | `rogue-ca` | | must be refused at the handshake |
