# Signing the in-app update artifacts

The updater has three layers of protection, applied in this order:

1. **Verified TLS** — the release feed and the download use certificate + hostname
   verification (`HttpClient::setVerifyTls`). Stops a network attacker swapping
   either one.
2. **SHA-256 digest** — the download is hashed as it streams and compared to the
   per-asset `sha256:` digest GitHub publishes in the releases API. Stops a
   corrupted or CDN-substituted file. Always on, no setup.
3. **Detached signature** — *this document*. Proves the artifact came from the
   project, so it survives a compromised release host, a stolen GitHub token, or
   a future TLS weakness. **Ships inert** until you add a key.

Layers 1–2 are active today. Layer 3 is the one that needs a key, because only
you can hold the private half.

## Current state: inert

`src/utils/update_verify.cpp` contains:

```cpp
const char kUpdatePublicKeyPem[] = "";
```

While that string is empty, `updateSignatureEnforced()` returns false, the
updater never fetches a `.sig`, and `verifyUpdateFile()` is a pass-through —
behaviour is exactly as before. Adding a key is the only switch.

## Activating

### 1. Generate a P-256 keypair

```sh
openssl ecparam -name prime256v1 -genkey -noout -out update-signing.key
openssl ec -in update-signing.key -pubout -out update-signing.pub
cat update-signing.pub
```

### 2. Store the private key as a CI secret

Add the **private** key (`update-signing.key`, whole PEM) as the repository
secret `UPDATE_SIGNING_KEY`. Never commit it. Keep an offline backup — losing it
means clients pinned to the public key can no longer be updated in place.

### 3. Paste the public key into the client

Replace the empty literal in `src/utils/update_verify.cpp` with the contents of
`update-signing.pub`, one C string per line, newline-terminated:

```cpp
const char kUpdatePublicKeyPem[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE...\n"
    "-----END PUBLIC KEY-----\n";
```

### 4. Enable the CI signing step

`.github/workflows/build-multi-platform.yml` already has a "Sign release
artifacts" step. It is skipped while `UPDATE_SIGNING_KEY` is unset; once the
secret exists it signs every asset and uploads `<asset>.sig` beside it.

The signature is raw DER ECDSA-P256 over the file's SHA-256 — what
`mbedtls_pk_verify` expects, i.e. plain `openssl dgst -sha256 -sign`.

## Rollout order matters

Clients only enforce a signature if **their** build has the key compiled in, and
they fetch `<asset>.sig` only then. So:

- Ship the key in a release **before** you start requiring it — old clients
  ignore `.sig` files, new clients fail closed if a `.sig` is missing.
- Once a signed release is out, every later release **must** be signed, or
  updated clients will refuse it (they delete the download and report
  "signature check failed").

## Platform coverage

Verification uses mbedcrypto, linked everywhere the app installs updates in
place: Vita, Switch, PS4, Android, and desktop Linux/macOS. **Windows** links
Schannel with no mbedcrypto and compiles the stub, so it relies on layers 1–2.
macOS falls back to the stub if Homebrew's keg-only mbedtls headers can't be
found at configure time (CMake warns).
