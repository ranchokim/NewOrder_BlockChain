# NewOrder Performance Report

Measured on the C implementation (WSL Ubuntu, gcc -O2):

```bash
cd src
make
./neworder_c bench 100000
```

## Results

| Workload | Size | Time | Throughput |
| --- | ---: | ---: | ---: |
| Add to mempool | 100,000 tx | ~0.17 s | ~578,000 tx/s |
| Produce block  | 100,000 tx | ~0.09 s | ~1,140,000 tx/s equivalent |

## Design

The C implementation maintains indexed account balances instead of scanning the
full chain for every transaction. This removes the O(N²) bottleneck that would
appear in a linear-scan design as the mempool grows.

Key properties:
- SHA-256 (FIPS 180-4) for all block hashes, transaction IDs, and addresses
- Indexed `NoAccount` table for O(1) balance lookups
- Indexed `NoTokenBalance` table for O(token × address) token balance lookups
- JSON persistence: chain written on every block, AI payments written on update
- Single-threaded HTTP/1.0 server; no locking required

## Known Limitations

- HTTP server is single-threaded; one slow client blocks all others
- JSON persistence writes the full chain file on every block; not suitable for
  large chains
- No P2P gossip: nodes are standalone, chain sync must be done manually
- Signature verification is not implemented; the node trusts submitted addresses
- AI responses are deterministic local stubs, not connected to real AI services

## Smoke Test

```bash
./neworder_c smoke
```

Exercises 5 PoA blocks covering: native transfer, token create/transfer,
counter contract deploy/call, AI payment create/verify/consume, SHA-256
self-test.
