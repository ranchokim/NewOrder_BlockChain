# NewOrder Performance Report

Measured on the current Python implementation with:

```powershell
python -B benchmarks\performance_benchmark.py --sign-count 1000 --sizes 100,500,1000 --read-size 1000 --read-samples 50
python -B benchmarks\performance_benchmark.py --sign-count 1000 --sizes 2000 --read-size 2000 --read-samples 30 --keep-data
```

## Results

| Workload | Size | Time | Throughput / latency |
| --- | ---: | ---: | ---: |
| Sign transactions | 1000 tx | 0.368 s | 2715 tx/s |
| Verify signatures | 1000 tx | 0.015 s | 68373 verify/s |
| Add to mempool | 100 tx | 0.067 s | 1493 tx/s, p95 1.25 ms |
| Add to mempool | 500 tx | 1.665 s | 300 tx/s, p95 6.28 ms |
| Add to mempool | 1000 tx | 6.645 s | 150 tx/s, p95 12.56 ms |
| Add to mempool | 2000 tx | 26.791 s | 74.7 tx/s, p95 25.39 ms |
| Produce block | 1000 tx | 0.026 s | 38047 tx/s equivalent |
| Produce block | 2000 tx | 0.052 s | 38642 tx/s equivalent |
| Balance lookup | 1000 tx chain | p95 0.076 ms | linear chain scan |
| Balance lookup | 2000 tx chain | p95 0.153 ms | linear chain scan |
| AI payment verify | 1000 tx chain | p95 0.594 ms | linear chain scan |
| AI payment verify | 2000 tx chain | p95 0.658 ms | linear chain scan |

## Findings

The current implementation is not suitable for a real payment network. It is
fine for a local demo, but the mempool path degrades sharply as pending
transactions grow.

The main bottleneck is `Blockchain.add_transaction()`. Each new transaction calls
`validate_transaction()`, which calls `balance_of()`. `balance_of()` scans every
block and every pending mempool transaction. Adding N pending transactions
therefore becomes close to O(N^2).

Block production looks fast in this small benchmark, but that number is
misleading for production. The implementation writes the whole chain to one
pretty-printed JSON file on every block, and block propagation, peer consensus,
fork handling, database durability, and HTTP load were not included.

AI payment verification also scans the chain linearly to find a matching payment
memo. That is acceptable for a demo, but it needs a transaction/payment index for
production.

## Recommendation

Before any real payment use, replace the current storage and state model:

- Maintain indexed account balances instead of scanning the chain for every tx.
- Track per-account pending debits in the mempool.
- Use a real embedded database such as RocksDB, SQLite WAL, or LMDB instead of a
  single JSON chain file.
- Add transaction indexes by `txid`, address, and AI payment memo.
- Batch block validation and state commits.
- Move cryptography to audited libraries.
- Put HTTP handling behind an async or multi-process service if keeping Python.

For a serious payment network, the core ledger should likely move to Rust, Go,
or another compiled runtime. Python can still remain useful for the wallet CLI,
admin tooling, explorer, AI orchestration, and tests.

## C Port Check

The C ledger port was compiled and tested in WSL with:

```bash
cd /mnt/c/AI_workspace/workspace_19/src
make clean
make
./neworder_c smoke
./neworder_c bench 100000
```

Result:

| C workload | Size | Time | Throughput |
| --- | ---: | ---: | ---: |
| Add to mempool | 100000 tx | 0.123 s | 815568 tx/s |
| Produce block | 100000 tx | 0.036 s | 2808121 tx/s equivalent |

The C result is much faster because it maintains indexed account balances and
pending debits instead of recalculating balances by scanning the full chain and
mempool for every transaction.
