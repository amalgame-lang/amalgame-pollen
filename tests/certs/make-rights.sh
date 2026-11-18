#!/bin/bash
# Re-issue the node certificates of the tests with their v0.8.0 rights
# (subjectAltName URIs), signed by ca.pem. ca.*, rogue-ca.* and
# intruder.* are kept. Run from tests/certs.
set -e
export MSYS_NO_PATHCONV=1
issue() {   # name  san
  openssl req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -subj "/CN=$1" \
    -keyout "$1.key" -out "$1.csr" 2>/dev/null
  printf 'subjectAltName=%s\n' "$2" > "$1.ext"
  openssl x509 -req -in "$1.csr" -CA ca.pem -CAkey ca.key -CAcreateserial -days 36500 \
    -extfile "$1.ext" -out "$1.pem" 2>/dev/null
  rm -f "$1.csr" "$1.ext"
}
issue node-a    "DNS:node-a,URI:pollen:publish:tls.*,URI:test:site:alpha"
issue node-b    "DNS:node-b,URI:pollen:publish:tls.test,URI:test:site:beta,URI:test:role:edge"
issue node-c    "DNS:node-c,URI:pollen:publish:other.*,URI:test:site:gamma"
issue node-none "DNS:node-none"
rm -f ca.srl
openssl verify -CAfile ca.pem node-a.pem node-b.pem node-c.pem node-none.pem
