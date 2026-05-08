# NewOrder

NewOrder is a small educational blockchain ecosystem available in both **Python**
and **C**. The coin name is **NewOrder** and the symbol is **NO**.

It includes:

- Coin metadata and Proof-of-Authority round-robin blocks
- Persistent chain storage in JSON
- Mempool and validator rewards
- Built-in smart contracts for counters and key-value state
- Token issuance and token transfer transactions
- AI service payment requests settled with NO coin transfers
- HTTP explorer and node API
- Wallet CLI for addresses, balances, transactions, AI payments, token workflows, and validator block production
- JSON-line TCP P2P node gossip *(Python only)*

This is a local development chain, not production cryptocurrency software.

---

## Quick Start — C

Build all three C binaries first:

```powershell
cd c_neworder
.\build.ps1          # Windows (gcc, clang, or MSVC)
```

```bash
cd c_neworder
make                 # Linux / macOS
```

Run the built-in smoke test:

```powershell
.\neworder_c.exe smoke
```

```bash
./neworder_c smoke
```

Run a benchmark (100 000 transactions):

```powershell
.\neworder_c.exe bench 100000
```

```bash
./neworder_c bench 100000
```

### C Node Server

Create validator addresses, then start the HTTP node:

```powershell
.\neworder_wallet.exe create --wallet validator1.json
.\neworder_wallet.exe create --wallet validator2.json
.\neworder_wallet.exe create --wallet validator3.json
```

```bash
./neworder_wallet create --wallet validator1.json
./neworder_wallet create --wallet validator2.json
./neworder_wallet create --wallet validator3.json
```

Start the node (supply the three generated `NO...` addresses):

```powershell
.\neworder_node.exe --port 8080 --data-dir data\node1 --validator NO... --validators NO1...,NO2...,NO3...
```

```bash
./neworder_node --port 8080 --data-dir data/node1 --validator NO... --validators NO1...,NO2...,NO3...
```

### C Wallet Commands

```powershell
.\neworder_wallet.exe create --wallet wallet1.json
.\neworder_wallet.exe address --wallet wallet1.json
.\neworder_wallet.exe balance --wallet wallet1.json
.\neworder_wallet.exe send --wallet wallet1.json --to NO... --amount 1 --fee 0.01 --memo "hello"
.\neworder_wallet.exe mine --wallet wallet1.json
.\neworder_wallet.exe ai-services
.\neworder_wallet.exe ai-payment-create --wallet wallet1.json --service-id chat-basic --units 2
.\neworder_wallet.exe ai-pay --wallet wallet1.json --payment-id AIP...
.\neworder_wallet.exe ai-verify --payment-id AIP...
.\neworder_wallet.exe ai-consume --wallet wallet1.json --payment-id AIP... --prompt "hello"
.\neworder_wallet.exe token-create --wallet wallet1.json --name "Example Token" --symbol EXT --supply 1000000
.\neworder_wallet.exe token-send --wallet wallet1.json --token NOT... --to NO... --amount 500
.\neworder_wallet.exe contract-deploy --wallet wallet1.json --name Votes --type counter
.\neworder_wallet.exe contract-call --wallet wallet1.json --contract NOC... --method increment --amount 1
```

*(Replace `.\neworder_wallet.exe` with `./neworder_wallet` on Linux/macOS.)*

---

## Quick Start — Python

```powershell
python -m neworder.wallet create --wallet validator1.json
python -m neworder.wallet create --wallet validator2.json
python -m neworder.wallet create --wallet validator3.json
```

Use the three generated `NO...` addresses as the shared validator list:

```powershell
python -m neworder --host 127.0.0.1 --port 8080 --p2p-port 9333 --data-dir data/node1 --validator-wallet validator1.json --validators NO...,NO...,NO...
```

Create a wallet:

```powershell
python -m neworder.wallet create --wallet wallet1.json
```

Mine a block:

```powershell
python -m neworder.wallet --wallet wallet1.json mine
```

Check balance:

```powershell
python -m neworder.wallet --wallet wallet1.json balance
```

Open explorer stats:

```powershell
Invoke-RestMethod http://127.0.0.1:8080/stats
```

### Python Wallet Commands

```powershell
python -m neworder.wallet create --wallet wallet1.json
python -m neworder.wallet --wallet wallet1.json address
python -m neworder.wallet --wallet wallet1.json balance
python -m neworder.wallet --wallet wallet1.json send --to NO... --amount 1 --fee 0.01 --memo "hello"
python -m neworder.wallet ai-services
python -m neworder.wallet --wallet wallet1.json ai-payment-create --service-id chat-basic --units 2
python -m neworder.wallet --wallet wallet1.json ai-pay --payment-id AIP...
python -m neworder.wallet ai-verify --payment-id AIP...
python -m neworder.wallet ai-consume --payment-id AIP... --prompt "hello"
python -m neworder.wallet --wallet wallet1.json token-create --name "Example Token" --symbol EXT --supply 1000000
python -m neworder.wallet --wallet wallet1.json contract-deploy --name Votes --contract-type counter
python -m neworder.wallet --wallet wallet1.json mine
```

---

## AI Payment Flow

The AI payment gateway uses normal NewOrder coin transfers as settlement. A node
creates an AI payment request with a service, customer address, amount due,
merchant address, and required memo. The customer pays by sending NO to the
merchant address with that memo. After the payment transaction is included in a
PoA block, `/ai/payments/{payment_id}/verify` marks the request as paid and
`/ai/payments/{payment_id}/consume` spends one paid service unit.

**C node** — set the AI merchant address via `--ai-merchant`:

```powershell
.\neworder_node.exe --port 8080 --data-dir data\node1 --validator NO... --validators NO1...,NO2...,NO3... --ai-merchant NO...
```

**Python node** — set it via `--ai-merchant-address` (defaults to the validator wallet address):

```powershell
python -m neworder --data-dir data/node1 --validator-wallet validator1.json --validators NO1...,NO2...,NO3... --ai-merchant-address NO...
```

---

## Running Multiple Nodes — C

Terminal 1:

```powershell
.\neworder_node.exe --port 8080 --data-dir data\node1 --validator NO1... --validators NO1...,NO2...,NO3...
```

Terminal 2:

```powershell
.\neworder_node.exe --port 8081 --data-dir data\node2 --validator NO2... --validators NO1...,NO2...,NO3...
```

Terminal 3:

```powershell
.\neworder_node.exe --port 8082 --data-dir data\node3 --validator NO3... --validators NO1...,NO2...,NO3...
```

## Running Multiple Nodes — Python

Terminal 1:

```powershell
python -m neworder --port 8080 --p2p-port 9333 --data-dir data/node1 --validator-wallet validator1.json --validators NO1...,NO2...,NO3...
```

Terminal 2:

```powershell
python -m neworder --port 8081 --p2p-port 9334 --data-dir data/node2 --validator-wallet validator2.json --validators NO1...,NO2...,NO3...
```

Terminal 3:

```powershell
python -m neworder --port 8082 --p2p-port 9335 --data-dir data/node3 --validator-wallet validator3.json --validators NO1...,NO2...,NO3...
```

Connect node 2 to node 1 (Python only — the C node does not implement P2P gossip):

```powershell
Invoke-RestMethod http://127.0.0.1:8081/peers -Method Post -ContentType "application/json" -Body '{"peer":"127.0.0.1:9333"}'
```

---

## Explorer API

Both the C node (`neworder_node`) and the Python node expose the same REST API:

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

Python-only endpoints:

- `GET /peers`
- `POST /peers` with `{"peer":"127.0.0.1:9334"}`

---

## C Implementation Notes

- **Cryptography**: SHA-256 (FIPS 180-4) for block hashes, transaction IDs, and address derivation. The C wallet uses a SHA-256-based key scheme (private key = 32 random bytes; address = `NO` + SHA-256 of public key). Python wallets use RSA-512 and are not directly compatible with C wallet key files.
- **Tokens**: identified by `NOT{hash}` prefix; balances maintained in an indexed table.
- **Smart contracts**: built-in `counter` (increment/decrement/reset) and `key_value` (set/delete) types, identified by `NOC{hash}` prefix.
- **HTTP server**: single-threaded, cross-platform (Winsock2 on Windows, POSIX sockets elsewhere), HTTP/1.0.
- **JSON persistence**: chain and AI payments are written and read back as plain JSON; no external library dependency.
- **P2P gossip**: not implemented in the C port (Python only).

---

## Wiki

The project wiki is in [wiki/Home.md](wiki/Home.md). It covers the chain
overview, Proof-of-Authority consensus, node and wallet usage, smart contracts, token
issuance, and explorer API routes.

There is also a static web version at [wiki-web/index.html](wiki-web/index.html)
that can be opened directly in a browser.

---

## Notes

The wallet and transaction signing are intentionally simple and are suitable for
local experimentation only. Real cryptocurrency software needs audited
cryptography, durable consensus rules, peer reputation, fork choice, wallet
encryption, and protection against many network-level attacks.
