# Test certificates — do not use outside the tests

PKI for `tests/tls_smoke.am`, generated with OpenSSL (EC P-256, valid
100 years). The private keys are public on purpose : these files only
exist so the test can run anywhere.

| File | Signed by | Role in the test |
|---|---|---|
| `ca.pem` / `ca.key` | itself | the "site" CA |
| `node-a.*` | `ca` | the test process (listener) |
| `node-b.*` | `ca` | a legitimate second node |
| `rogue-ca.*` | itself | another CA |
| `intruder.*` | `rogue-ca` | must be refused |
