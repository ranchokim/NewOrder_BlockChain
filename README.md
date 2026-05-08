# NewOrder

NewOrder is a small educational blockchain ecosystem implemented with Python's
standard library. The coin name is **NewOrder** and the symbol is **NO**.

It includes:

- Coin metadata and Proof-of-Authority blocks
- Persistent chain storage in JSON
- Mempool and validator rewards
- Built-in smart contracts for counters and key-value state
- Token issuance and token transfer transactions
- JSON-line TCP P2P node gossip
- HTTP explorer and node API
- Wallet CLI for addresses, balances, transactions, token workflows, and validator block production

This is a local development chain, not production cryptocurrency software.

## Quick Start

Create validator wallets, then run a validator node and explorer API:

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

Ask the current validator node to produce the next block and pay the reward to the wallet:

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

## Wallet Commands

```powershell
python -m neworder.wallet create --wallet wallet1.json
python -m neworder.wallet --wallet wallet1.json address
python -m neworder.wallet --wallet wallet1.json balance
python -m neworder.wallet --wallet wallet1.json send --to NO... --amount 1 --fee 0.01 --memo "hello"
python -m neworder.wallet --wallet wallet1.json token-create --name "Example Token" --symbol EXT --supply 1000000
python -m neworder.wallet --wallet wallet1.json contract-deploy --name Votes --contract-type counter
python -m neworder.wallet --wallet wallet1.json mine
```

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
- `GET /mine?address={NO...}`
- `GET /peers`
- `POST /transactions`
- `POST /peers` with `{"peer":"127.0.0.1:9334"}`

## Wiki

The project wiki is in [wiki/Home.md](wiki/Home.md). It covers the chain
overview, Proof-of-Authority consensus, node and wallet usage, smart contracts, token
issuance, and explorer API routes.

There is also a static web version at [wiki-web/index.html](wiki-web/index.html)
that can be opened directly in a browser.

## Running Multiple Nodes

Each node must use the same `--validators` list and a different validator wallet from that list.

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

Connect node 2 to node 1:

```powershell
Invoke-RestMethod http://127.0.0.1:8081/peers -Method Post -ContentType "application/json" -Body '{"peer":"127.0.0.1:9333"}'
```

## Notes

The wallet and transaction signing are intentionally simple and are suitable for
local experimentation only. Real cryptocurrency software needs audited
cryptography, durable consensus rules, peer reputation, fork choice, wallet
encryption, and protection against many network-level attacks.
