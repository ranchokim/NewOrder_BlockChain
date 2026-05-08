# NewOrder C Port

This directory contains a C port of the performance-critical NewOrder ledger
core.

Implemented:

- Native NO account addresses
- Indexed account balances
- Pending debit tracking in the mempool
- Transfer validation
- Block production
- AI payment creation
- AI payment verification from confirmed transfers
- AI payment unit consumption
- Smoke-test CLI
- Ledger throughput benchmark CLI

Not implemented in this C layer yet:

- HTTP explorer API
- TCP P2P gossip
- Persistent database storage
- Wallet file format compatibility with the Python prototype
- Audited cryptography

The intent is to move the hot ledger path away from the Python implementation's
chain-wide scans while keeping the higher-level Python prototype available during
the transition.

## Build

Windows PowerShell:

```powershell
.\build.ps1
.\neworder_c.exe smoke
.\neworder_c.exe bench 100000
```

Make:

```bash
make
./neworder_c smoke
./neworder_c bench 100000
```
