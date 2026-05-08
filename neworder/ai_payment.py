"""AI service payment gateway backed by NewOrder coin transfers."""

from __future__ import annotations

import json
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path
from threading import RLock
from typing import Any

from .blockchain import Blockchain
from .coin import COIN
from .crypto import sha256_hex


PAYMENT_MEMO_PREFIX = "AIPAY"


@dataclass(frozen=True)
class AIService:
    service_id: str
    name: str
    price_per_unit: float
    unit_name: str
    description: str

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class AIPayment:
    payment_id: str
    service_id: str
    customer_address: str
    merchant_address: str
    units: int
    amount_due: float
    memo: str
    status: str = "pending"
    paid_txid: str = ""
    created_at: float = field(default_factory=time.time)
    paid_at: float | None = None
    consumed_units: int = 0

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


DEFAULT_SERVICES = {
    "chat-basic": AIService(
        service_id="chat-basic",
        name="Basic AI Chat",
        price_per_unit=0.25,
        unit_name="prompt",
        description="Deterministic local chat response for demos and tests.",
    ),
    "summary": AIService(
        service_id="summary",
        name="Document Summary",
        price_per_unit=0.75,
        unit_name="document",
        description="Short deterministic summary response for submitted text.",
    ),
}


class AIPaymentGateway:
    def __init__(
        self,
        blockchain: Blockchain,
        merchant_address: str,
        data_dir: str | Path,
        services: dict[str, AIService] | None = None,
    ):
        self.blockchain = blockchain
        self.merchant_address = merchant_address
        self.services = services or DEFAULT_SERVICES
        self.path = Path(data_dir) / "ai_payments.json"
        self.lock = RLock()
        self.payments: dict[str, AIPayment] = {}
        self.load()

    def load(self) -> None:
        if not self.path.exists():
            return
        with self.path.open("r", encoding="utf-8") as fh:
            self.payments = {
                item["payment_id"]: AIPayment(**item)
                for item in json.load(fh)
            }

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.path.open("w", encoding="utf-8") as fh:
            json.dump([payment.to_dict() for payment in self.payments.values()], fh, indent=2)

    def list_services(self) -> list[dict[str, Any]]:
        return [service.to_dict() for service in self.services.values()]

    def create_payment(self, service_id: str, customer_address: str, units: int) -> AIPayment:
        if service_id not in self.services:
            raise ValueError("unknown service_id")
        if not customer_address.startswith(COIN.symbol):
            raise ValueError(f"customer_address must be a {COIN.symbol} address")
        if units <= 0:
            raise ValueError("units must be positive")

        service = self.services[service_id]
        amount_due = round(service.price_per_unit * units, COIN.decimals)
        seed = f"{service_id}:{customer_address}:{units}:{time.time_ns()}"
        payment_id = "AIP" + sha256_hex(seed)[:37]
        payment = AIPayment(
            payment_id=payment_id,
            service_id=service_id,
            customer_address=customer_address,
            merchant_address=self.merchant_address,
            units=units,
            amount_due=amount_due,
            memo=f"{PAYMENT_MEMO_PREFIX}:{payment_id}",
        )
        with self.lock:
            self.payments[payment_id] = payment
            self.save()
        return payment

    def get_payment(self, payment_id: str) -> AIPayment | None:
        return self.payments.get(payment_id)

    def verify_payment(self, payment_id: str) -> AIPayment:
        payment = self._require_payment(payment_id)
        if payment.status in {"paid", "consumed"}:
            return payment

        match = self._find_matching_transfer(payment)
        if match is None:
            return payment

        payment.status = "paid"
        payment.paid_txid = match["txid"]
        payment.paid_at = match["timestamp"]
        with self.lock:
            self.payments[payment_id] = payment
            self.save()
        return payment

    def consume(self, payment_id: str, prompt: str = "") -> dict[str, Any]:
        payment = self.verify_payment(payment_id)
        if payment.status not in {"paid", "consumed"}:
            raise ValueError("payment is not paid")
        if payment.consumed_units >= payment.units:
            raise ValueError("payment has no remaining units")

        service = self.services[payment.service_id]
        payment.consumed_units += 1
        if payment.consumed_units >= payment.units:
            payment.status = "consumed"
        with self.lock:
            self.payments[payment_id] = payment
            self.save()

        return {
            "payment": payment.to_dict(),
            "service": service.to_dict(),
            "result": self._local_ai_response(service, prompt),
        }

    def _require_payment(self, payment_id: str) -> AIPayment:
        payment = self.get_payment(payment_id)
        if payment is None:
            raise ValueError("payment not found")
        return payment

    def _find_matching_transfer(self, payment: AIPayment) -> dict[str, Any] | None:
        for block in self.blockchain.chain:
            for tx in block.transactions:
                if (
                    tx.tx_type == "transfer"
                    and tx.sender == payment.customer_address
                    and tx.recipient == payment.merchant_address
                    and tx.memo == payment.memo
                    and tx.amount >= payment.amount_due
                ):
                    return {"txid": tx.txid(), "block": block.index, "timestamp": tx.timestamp}
        return None

    def _local_ai_response(self, service: AIService, prompt: str) -> dict[str, Any]:
        normalized = " ".join(prompt.split())
        digest = sha256_hex(f"{service.service_id}:{normalized}")[:12]
        if service.service_id == "summary":
            words = normalized.split()
            summary = " ".join(words[:24]) if words else "No input text was provided."
            return {"type": "summary", "text": summary, "reference": digest}
        return {
            "type": "chat",
            "text": f"Processed paid NewOrder AI request {digest}.",
            "reference": digest,
        }
