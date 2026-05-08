"""Data models for the NewOrder ledger."""

from __future__ import annotations

import json
import time
from dataclasses import asdict, dataclass, field
from typing import Any

from .crypto import address_from_public, public_key_from_private, sha256_hex, sign


def canonical_json(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


@dataclass
class Transaction:
    sender: str
    recipient: str
    amount: float
    fee: float = 0.0
    timestamp: float = field(default_factory=time.time)
    public_key: str = ""
    signature: str = ""
    memo: str = ""
    tx_type: str = "transfer"
    data: dict[str, Any] = field(default_factory=dict)

    def signing_payload(self) -> str:
        return canonical_json(
            {
                "sender": self.sender,
                "recipient": self.recipient,
                "amount": self.amount,
                "fee": self.fee,
                "timestamp": self.timestamp,
                "memo": self.memo,
                "tx_type": self.tx_type,
                "data": self.data,
            }
        )

    def txid(self) -> str:
        return sha256_hex(canonical_json(self.to_dict(include_signature=True)))

    def to_dict(self, include_signature: bool = True) -> dict[str, Any]:
        data = asdict(self)
        if not include_signature:
            data.pop("signature", None)
        return data

    @classmethod
    def coinbase(cls, recipient: str, amount: float, memo: str = "") -> "Transaction":
        return cls(sender="COINBASE", recipient=recipient, amount=amount, memo=memo)

    @classmethod
    def signed(
        cls,
        private_key: str,
        recipient: str,
        amount: float,
        fee: float = 0.0,
        memo: str = "",
        tx_type: str = "transfer",
        data: dict[str, Any] | None = None,
    ) -> "Transaction":
        public_key = public_key_from_private(private_key)
        sender = address_from_public(public_key)
        tx = cls(
            sender=sender,
            recipient=recipient,
            amount=amount,
            fee=fee,
            public_key=public_key,
            memo=memo,
            tx_type=tx_type,
            data=data or {},
        )
        tx.signature = sign(private_key, tx.signing_payload())
        return tx

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "Transaction":
        return cls(**data)


@dataclass
class Block:
    index: int
    previous_hash: str
    transactions: list[Transaction]
    timestamp: float = field(default_factory=time.time)
    consensus: str = "poa-round-robin"
    validator: str = ""
    validator_public_key: str = ""
    validator_signature: str = ""

    def header(self) -> dict[str, Any]:
        return {
            "index": self.index,
            "previous_hash": self.previous_hash,
            "transactions": [tx.to_dict(include_signature=True) for tx in self.transactions],
            "timestamp": self.timestamp,
            "consensus": self.consensus,
            "validator": self.validator,
            "validator_public_key": self.validator_public_key,
        }

    def hash(self) -> str:
        return sha256_hex(canonical_json(self.header()))

    def signing_payload(self) -> str:
        return self.hash()

    def seal(self, private_key: str) -> str:
        self.validator_public_key = public_key_from_private(private_key)
        self.validator = address_from_public(self.validator_public_key)
        self.validator_signature = sign(private_key, self.signing_payload())
        return self.hash()

    def to_dict(self) -> dict[str, Any]:
        data = self.header()
        data["validator_signature"] = self.validator_signature
        data["hash"] = self.hash()
        return data

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "Block":
        transactions = [Transaction.from_dict(tx) for tx in data["transactions"]]
        return cls(
            index=data["index"],
            previous_hash=data["previous_hash"],
            transactions=transactions,
            timestamp=data["timestamp"],
            consensus=data.get("consensus", "poa-round-robin"),
            validator=data.get("validator", ""),
            validator_public_key=data.get("validator_public_key", ""),
            validator_signature=data.get("validator_signature", ""),
        )
