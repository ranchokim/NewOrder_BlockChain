"""Performance benchmark for the current NewOrder implementation."""

from __future__ import annotations

import argparse
import json
import shutil
import statistics
import sys
import time
import uuid
from pathlib import Path
from typing import Callable

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from neworder.ai_payment import AIPaymentGateway
from neworder.blockchain import Blockchain
from neworder.crypto import address_from_public, new_private_key, public_key_from_private, verify_signature
from neworder.models import Transaction


BENCH_TMP = ROOT / "tmp_benchmarks"


def now() -> float:
    return time.perf_counter()


def timed(func: Callable[[], object]) -> tuple[object, float]:
    start = now()
    result = func()
    return result, now() - start


def rate(count: int, seconds: float) -> float:
    if seconds <= 0:
        return float("inf")
    return count / seconds


def address_for(private_key: str) -> str:
    return address_from_public(public_key_from_private(private_key))


def fresh_dir(name: str) -> Path:
    path = BENCH_TMP / f"{name}_{uuid.uuid4().hex}"
    path.mkdir(parents=True)
    return path


def make_chain(name: str) -> tuple[Blockchain, str, str, str, str, str]:
    validator_key = new_private_key()
    customer_key = new_private_key()
    merchant_key = new_private_key()
    recipient_key = new_private_key()
    validator_address = address_for(validator_key)
    customer_address = address_for(customer_key)
    merchant_address = address_for(merchant_key)
    chain = Blockchain(fresh_dir(name), validators=[validator_address])
    chain.produce_pending(validator_key, reward_address=customer_address)
    return chain, validator_key, customer_key, customer_address, recipient_key, merchant_address


def signed_transfers(customer_key: str, recipient_address: str, count: int) -> list[Transaction]:
    return [
        Transaction.signed(
            private_key=customer_key,
            recipient=recipient_address,
            amount=0.001,
            memo=f"bench:{index}",
        )
        for index in range(count)
    ]


def percentile(values: list[float], pct: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, int(round((pct / 100) * (len(ordered) - 1))))
    return ordered[index]


def benchmark_sign_verify(count: int) -> dict[str, float]:
    private_key = new_private_key()
    recipient = address_for(new_private_key())
    txs, sign_seconds = timed(lambda: signed_transfers(private_key, recipient, count))
    _, verify_seconds = timed(lambda: [verify_signature(tx.public_key, tx.signing_payload(), tx.signature) for tx in txs])
    return {
        "transactions": count,
        "sign_seconds": sign_seconds,
        "sign_tps": rate(count, sign_seconds),
        "verify_seconds": verify_seconds,
        "verify_tps": rate(count, verify_seconds),
    }


def benchmark_mempool_and_block(sizes: list[int]) -> list[dict[str, float]]:
    rows = []
    for count in sizes:
        chain, validator_key, customer_key, customer_address, recipient_key, _ = make_chain(f"mempool_{count}")
        recipient_address = address_for(recipient_key)
        txs = signed_transfers(customer_key, recipient_address, count)

        add_latencies = []
        start = now()
        for tx in txs:
            tx_start = now()
            ok, reason = chain.add_transaction(tx)
            add_latencies.append(now() - tx_start)
            if not ok:
                raise RuntimeError(reason)
        add_seconds = now() - start
        _, block_seconds = timed(lambda: chain.produce_pending(validator_key))

        rows.append(
            {
                "transactions": count,
                "add_seconds": add_seconds,
                "add_tps": rate(count, add_seconds),
                "add_p50_ms": statistics.median(add_latencies) * 1000,
                "add_p95_ms": percentile(add_latencies, 95) * 1000,
                "block_seconds": block_seconds,
                "block_tps_equivalent": rate(count, block_seconds),
                "chain_json_bytes": chain.chain_path.stat().st_size,
            }
        )
    return rows


def benchmark_reads(count: int, samples: int) -> dict[str, float]:
    chain, validator_key, customer_key, customer_address, recipient_key, merchant_address = make_chain("reads")
    recipient_address = address_for(recipient_key)
    gateway = AIPaymentGateway(chain, merchant_address, chain.data_dir)
    payment = gateway.create_payment("chat-basic", customer_address, 1)

    txs = signed_transfers(customer_key, recipient_address, count - 1)
    txs.append(
        Transaction.signed(
            private_key=customer_key,
            recipient=merchant_address,
            amount=payment.amount_due,
            memo=payment.memo,
        )
    )
    for tx in txs:
        ok, reason = chain.add_transaction(tx)
        if not ok:
            raise RuntimeError(reason)
    chain.produce_pending(validator_key)

    balance_times = []
    for _ in range(samples):
        _, seconds = timed(lambda: chain.balance_of(customer_address))
        balance_times.append(seconds)

    verify_times = []
    for _ in range(samples):
        payment.status = "pending"
        payment.paid_txid = ""
        payment.paid_at = None
        _, seconds = timed(lambda: gateway.verify_payment(payment.payment_id))
        verify_times.append(seconds)

    return {
        "transactions_in_chain": count,
        "samples": samples,
        "balance_p50_ms": statistics.median(balance_times) * 1000,
        "balance_p95_ms": percentile(balance_times, 95) * 1000,
        "ai_verify_p50_ms": statistics.median(verify_times) * 1000,
        "ai_verify_p95_ms": percentile(verify_times, 95) * 1000,
        "chain_json_bytes": chain.chain_path.stat().st_size,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Benchmark the current NewOrder blockchain.")
    parser.add_argument("--sign-count", type=int, default=1000)
    parser.add_argument("--sizes", default="100,500,1000")
    parser.add_argument("--read-size", type=int, default=1000)
    parser.add_argument("--read-samples", type=int, default=50)
    parser.add_argument("--keep-data", action="store_true")
    args = parser.parse_args()

    if BENCH_TMP.exists() and not args.keep_data:
        shutil.rmtree(BENCH_TMP)
    BENCH_TMP.mkdir(exist_ok=True)

    sizes = [int(item.strip()) for item in args.sizes.split(",") if item.strip()]
    result = {
        "sign_verify": benchmark_sign_verify(args.sign_count),
        "mempool_and_block": benchmark_mempool_and_block(sizes),
        "reads": benchmark_reads(args.read_size, args.read_samples),
    }
    print(json.dumps(result, indent=2))

    if not args.keep_data:
        shutil.rmtree(BENCH_TMP, ignore_errors=True)


if __name__ == "__main__":
    main()
