# NewOrder

NewOrder is a small educational blockchain written in C. The coin name is
**NewOrder** and the symbol is **NO**.

Features:

- Coin metadata and Proof-of-Authority round-robin blocks
- Persistent chain storage in JSON
- Mempool and validator rewards
- Built-in smart contracts for counters and key-value state
- Token issuance and token transfer transactions
- AI service payment requests settled with NO coin transfers
- HTTP explorer and node API
- Wallet CLI for addresses, balances, transactions, AI payments, token workflows, and validator block production

This is a local development chain, not production cryptocurrency software.

---

## Build

```bash
cd src
make
```

Three binaries are produced:

| Binary | Purpose |
|---|---|
| `neworder_c` | Smoke test and benchmark |
| `neworder_node` | HTTP node server |
| `neworder_wallet` | Wallet CLI |

Requires gcc or clang with C11 support and `-lm`. Tested on Linux (WSL Ubuntu).

---

## Quick Start

Create validator wallets:

```bash
./neworder_wallet create --wallet validator1.json
./neworder_wallet create --wallet validator2.json
./neworder_wallet create --wallet validator3.json
```

Start the node (supply the three generated `NO...` addresses):

```bash
./neworder_node --port 8080 --data-dir data/node1 \
    --validator NO... --validators NO1...,NO2...,NO3...
```

Create a user wallet, mine a block, and check balance:

```bash
./neworder_wallet create --wallet wallet1.json
./neworder_wallet mine --wallet wallet1.json
./neworder_wallet balance --wallet wallet1.json
```

Check stats:

```bash
curl http://127.0.0.1:8080/stats
```

---

## Wallet Commands

```bash
./neworder_wallet create   --wallet wallet1.json
./neworder_wallet address  --wallet wallet1.json
./neworder_wallet balance  --wallet wallet1.json
./neworder_wallet send     --wallet wallet1.json --to NO... --amount 1 --fee 0.01 --memo "hello"
./neworder_wallet mine     --wallet wallet1.json
./neworder_wallet ai-services
./neworder_wallet ai-payment-create  --wallet wallet1.json --service-id chat-basic --units 2
./neworder_wallet ai-pay             --wallet wallet1.json --payment-id AIP...
./neworder_wallet ai-verify          --payment-id AIP...
./neworder_wallet ai-consume         --wallet wallet1.json --payment-id AIP... --prompt "hello"
./neworder_wallet token-create       --wallet wallet1.json --name "Example Token" --symbol EXT --supply 1000000
./neworder_wallet token-send         --wallet wallet1.json --token NOT... --to NO... --amount 500
./neworder_wallet contract-deploy    --wallet wallet1.json --name Votes --type counter
./neworder_wallet contract-call      --wallet wallet1.json --contract NOC... --method increment --amount 1
```

---

## AI Payment Flow

The AI payment gateway uses normal NewOrder coin transfers as settlement. A node
creates an AI payment request with a service, customer address, amount due,
merchant address, and required memo. The customer pays by sending NO to the
merchant address with that memo. After the payment transaction is included in a
PoA block, `/ai/payments/{payment_id}/verify` marks the request as paid and
`/ai/payments/{payment_id}/consume` spends one paid service unit.

Set the AI merchant address via `--ai-merchant` (defaults to `--validator`):

```bash
./neworder_node --port 8080 --data-dir data/node1 \
    --validator NO... --validators NO1...,NO2...,NO3... \
    --ai-merchant NO...
```

---

## Running Multiple Nodes

```bash
# Terminal 1
./neworder_node --port 8080 --data-dir data/node1 \
    --validator NO1... --validators NO1...,NO2...,NO3...

# Terminal 2
./neworder_node --port 8081 --data-dir data/node2 \
    --validator NO2... --validators NO1...,NO2...,NO3...

# Terminal 3
./neworder_node --port 8082 --data-dir data/node3 \
    --validator NO3... --validators NO1...,NO2...,NO3...
```

---

## Explorer API

- `GET /stats`
- `GET /chain`
- `GET /blocks/{height-or-hash}`
- `GET /tx/{txid}`
- `GET /address/{address}`
- `GET /mempool`
- `GET /tokens`
- `GET /tokens/{token_id}`
- `GET /tokens/{token_id}/balances/{address}`
- `GET /contracts`
- `GET /contracts/{contract_id}`
- `GET /ai/services`
- `GET /ai/payments/{payment_id}`
- `GET /mine?address={NO...}`
- `POST /transactions`
- `POST /ai/payments`
- `POST /ai/payments/{payment_id}/verify`
- `POST /ai/payments/{payment_id}/consume`

---

## Implementation Notes

- **Cryptography**: SHA-256 (FIPS 180-4) for block hashes, transaction IDs, and address derivation. Private key = 32 random bytes; address = `NO` + SHA-256 of public key. Signatures are not cryptographically verified by the node (educational simplification).
- **Tokens**: identified by `NOT{hash}` prefix; balances maintained in an indexed table.
- **Smart contracts**: built-in `counter` (increment/decrement/reset) and `key_value` (set/delete) types, identified by `NOC{hash}` prefix.
- **HTTP server**: single-threaded, POSIX sockets, HTTP/1.0.
- **JSON persistence**: chain and AI payments are written and read back as plain JSON; no external library dependency.

---

## Wiki

The project wiki is in [wiki/Home.md](wiki/Home.md). It covers the chain
overview, Proof-of-Authority consensus, node and wallet usage, smart contracts,
token issuance, and explorer API routes.

There is also a static web version at [wiki-web/index.html](wiki-web/index.html)
that can be opened directly in a browser.

---

## Notes

The wallet and transaction signing are intentionally simple and are suitable for
local experimentation only. Real cryptocurrency software needs audited
cryptography, durable consensus rules, peer reputation, fork choice, wallet
encryption, and protection against many network-level attacks.
