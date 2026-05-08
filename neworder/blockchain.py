"""NewOrder blockchain state, validation, and persistence."""

from __future__ import annotations

import json
from pathlib import Path
from threading import RLock
from typing import Any

from .coin import COIN
from .crypto import address_from_public, public_key_from_private, sha256_hex, verify_signature
from .models import Block, Transaction


TOKEN_SYMBOL_MAX_LENGTH = 12
CONTRACT_TYPES = {"counter", "key_value"}


class Blockchain:
    def __init__(self, data_dir: str | Path = "data", validators: list[str] | None = None):
        self.data_dir = Path(data_dir)
        self.chain_path = self.data_dir / "chain.json"
        self.lock = RLock()
        self.chain: list[Block] = []
        self.mempool: list[Transaction] = []
        self.validators = validators or []
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self.load()

    def load(self) -> None:
        if self.chain_path.exists():
            with self.chain_path.open("r", encoding="utf-8") as fh:
                self.chain = [Block.from_dict(block) for block in json.load(fh)]
        else:
            self.chain = [self.create_genesis_block()]
            self.save()

    def save(self) -> None:
        with self.chain_path.open("w", encoding="utf-8") as fh:
            json.dump([block.to_dict() for block in self.chain], fh, indent=2)

    def create_genesis_block(self) -> Block:
        tx = Transaction.coinbase("NO_GENESIS", 0, COIN.genesis_message)
        tx.timestamp = 0.0
        return Block(index=0, previous_hash="0" * 64, transactions=[tx], timestamp=0.0, validator="GENESIS")

    @property
    def last_block(self) -> Block:
        return self.chain[-1]

    def height(self) -> int:
        return len(self.chain) - 1

    def balance_of(self, address: str) -> float:
        balance = 0.0
        for block in self.chain:
            for tx in block.transactions:
                if tx.sender == address and tx.sender != "COINBASE":
                    balance -= tx.amount + tx.fee
                if tx.recipient == address:
                    balance += tx.amount
        for tx in self.mempool:
            if tx.sender == address and tx.sender != "COINBASE":
                balance -= tx.amount + tx.fee
        return round(balance, COIN.decimals)

    def supply(self) -> float:
        # Coinbase payouts include base reward + collected fees. Fees were already
        # deducted from senders, so subtract them back out to avoid double-counting.
        minted = 0.0
        for block in self.chain:
            for tx in block.transactions:
                if tx.sender == "COINBASE":
                    minted += tx.amount
                else:
                    minted -= tx.fee
        return round(minted, COIN.decimals)

    def validate_transaction(self, tx: Transaction, allow_coinbase: bool = False) -> tuple[bool, str]:
        if tx.sender == "COINBASE":
            return (allow_coinbase, "coinbase is only valid inside mined blocks")
        if tx.tx_type not in {"transfer", "token_create", "token_transfer", "contract_deploy", "contract_call"}:
            return False, f"unsupported transaction type: {tx.tx_type}"
        if tx.tx_type == "transfer" and tx.amount <= 0:
            return False, "amount must be positive"
        if tx.tx_type != "transfer" and tx.amount < 0:
            return False, "amount cannot be negative"
        if tx.fee < 0:
            return False, "fee cannot be negative"
        if tx.tx_type == "transfer" and not tx.recipient.startswith(COIN.symbol):
            return False, f"recipient must be a {COIN.symbol} address"
        if not tx.public_key or address_from_public(tx.public_key) != tx.sender:
            return False, "public key does not match sender address"
        if not verify_signature(tx.public_key, tx.signing_payload(), tx.signature):
            return False, "invalid transaction signature"
        if self.balance_of(tx.sender) < tx.amount + tx.fee:
            return False, "insufficient balance"
        if tx.tx_type == "token_create":
            return self._validate_token_create(tx)
        if tx.tx_type == "token_transfer":
            return self._validate_token_transfer(tx)
        if tx.tx_type == "contract_deploy":
            return self._validate_contract_deploy(tx)
        if tx.tx_type == "contract_call":
            return self._validate_contract_call(tx)
        return True, "valid"

    def _validate_token_create(self, tx: Transaction) -> tuple[bool, str]:
        symbol = str(tx.data.get("symbol", "")).upper()
        name = str(tx.data.get("name", "")).strip()
        total_supply = float(tx.data.get("total_supply", 0))
        decimals = int(tx.data.get("decimals", 0))
        if not symbol.isalnum() or not 1 <= len(symbol) <= TOKEN_SYMBOL_MAX_LENGTH:
            return False, "token symbol must be 1-12 alphanumeric characters"
        if symbol == COIN.symbol:
            return False, "token symbol cannot equal the native coin symbol"
        if not name:
            return False, "token name is required"
        if total_supply <= 0:
            return False, "token total_supply must be positive"
        if decimals < 0 or decimals > 18:
            return False, "token decimals must be between 0 and 18"
        return True, "valid"

    def _validate_token_transfer(self, tx: Transaction) -> tuple[bool, str]:
        token_id = str(tx.data.get("token_id", ""))
        recipient = str(tx.data.get("to", ""))
        amount = float(tx.data.get("amount", 0))
        if token_id not in self.tokens():
            return False, "unknown token_id"
        if not recipient.startswith(COIN.symbol):
            return False, f"token recipient must be a {COIN.symbol} address"
        if amount <= 0:
            return False, "token amount must be positive"
        if self.token_balance_of(token_id, tx.sender, include_mempool=True) < amount:
            return False, "insufficient token balance"
        return True, "valid"

    def _validate_contract_deploy(self, tx: Transaction) -> tuple[bool, str]:
        contract_type = str(tx.data.get("contract_type", ""))
        name = str(tx.data.get("name", "")).strip()
        if contract_type not in CONTRACT_TYPES:
            return False, f"contract_type must be one of: {', '.join(sorted(CONTRACT_TYPES))}"
        if not name:
            return False, "contract name is required"
        return True, "valid"

    def _validate_contract_call(self, tx: Transaction) -> tuple[bool, str]:
        contract_id = str(tx.data.get("contract_id", ""))
        method = str(tx.data.get("method", ""))
        contracts = self.contracts()
        if contract_id not in contracts:
            return False, "unknown contract_id"
        contract_type = contracts[contract_id]["contract_type"]
        if contract_type == "counter" and method not in {"increment", "decrement", "reset"}:
            return False, "unsupported counter method"
        if contract_type == "key_value" and method not in {"set", "delete"}:
            return False, "unsupported key_value method"
        return True, "valid"

    def add_transaction(self, tx: Transaction) -> tuple[bool, str]:
        with self.lock:
            ok, reason = self.validate_transaction(tx)
            if not ok:
                return ok, reason
            if any(existing.txid() == tx.txid() for existing in self.mempool):
                return False, "transaction already in mempool"
            self.mempool.append(tx)
            return True, tx.txid()

    def pending_fees(self) -> float:
        return round(sum(tx.fee for tx in self.mempool), COIN.decimals)

    def expected_validator(self, block_index: int) -> str:
        if not self.validators:
            return ""
        return self.validators[(block_index - 1) % len(self.validators)]

    def next_validator(self) -> str:
        return self.expected_validator(self.last_block.index + 1)

    def can_produce(self, validator_address: str) -> bool:
        return bool(validator_address and validator_address == self.next_validator())

    def produce_pending(self, validator_private_key: str, reward_address: str | None = None) -> Block:
        with self.lock:
            validator_public_key = public_key_from_private(validator_private_key)
            validator_address = address_from_public(validator_public_key)
            expected = self.next_validator()
            if expected and validator_address != expected:
                raise ValueError(f"not this validator's turn; expected {expected}")
            fees = self.pending_fees()
            reward = min(COIN.block_reward + fees, COIN.max_supply - self.supply())
            coinbase = Transaction.coinbase(reward_address or validator_address, reward, f"{COIN.name} validator reward")
            block = Block(
                index=self.last_block.index + 1,
                previous_hash=self.last_block.hash(),
                transactions=[coinbase, *self.mempool],
            )
            block.seal(validator_private_key)
            self.chain.append(block)
            self.mempool = []
            self.save()
            return block

    def mine_pending(self, miner_address: str) -> Block:
        if not self.validators:
            raise ValueError("no validators configured for PoA block production")
        raise ValueError("PoA requires a validator private key; use produce_pending")

    def validate_block(self, block: Block, previous: Block) -> tuple[bool, str]:
        if block.index != previous.index + 1:
            return False, "bad block index"
        if block.previous_hash != previous.hash():
            return False, "previous hash mismatch"
        if block.consensus != "poa-round-robin":
            return False, "unsupported consensus"
        if self.validators:
            expected = self.expected_validator(block.index)
            if block.validator != expected:
                return False, f"wrong validator for height; expected {expected}"
        if not block.validator or block.validator == "GENESIS":
            return False, "missing validator"
        if not block.validator_public_key or address_from_public(block.validator_public_key) != block.validator:
            return False, "validator public key does not match address"
        if not verify_signature(block.validator_public_key, block.signing_payload(), block.validator_signature):
            return False, "invalid validator signature"
        coinbase_count = sum(1 for tx in block.transactions if tx.sender == "COINBASE")
        if coinbase_count != 1:
            return False, "block must contain exactly one coinbase transaction"
        coinbase_tx = next(tx for tx in block.transactions if tx.sender == "COINBASE")
        expected_fees = sum(tx.fee for tx in block.transactions if tx.sender != "COINBASE")
        if coinbase_tx.amount > COIN.block_reward + expected_fees:
            return False, "coinbase amount exceeds block reward plus collected fees"
        for tx in block.transactions:
            if tx.sender != "COINBASE":
                ok, reason = self.validate_transaction(tx)
                if not ok:
                    return False, reason
        return True, "valid"

    def add_block(self, block: Block) -> tuple[bool, str]:
        with self.lock:
            ok, reason = self.validate_block(block, self.last_block)
            if not ok:
                return ok, reason
            self.chain.append(block)
            mined_txids = {tx.txid() for tx in block.transactions}
            self.mempool = [tx for tx in self.mempool if tx.txid() not in mined_txids]
            self.save()
            return True, block.hash()

    def replace_chain(self, blocks: list[Block]) -> bool:
        if len(blocks) <= len(self.chain):
            return False
        if not self.is_valid_chain(blocks):
            return False
        with self.lock:
            if len(blocks) <= len(self.chain):
                return False
            self.chain = blocks
            mined_txids = {tx.txid() for block in blocks for tx in block.transactions}
            self.mempool = [tx for tx in self.mempool if tx.txid() not in mined_txids]
            self.save()
        return True

    def is_valid_chain(self, chain: list[Block]) -> bool:
        if not chain:
            return False
        for index in range(1, len(chain)):
            current = chain[index]
            previous = chain[index - 1]
            if current.previous_hash != previous.hash():
                return False
            if self.validate_block(current, previous)[0] is False:
                return False
        return True

    def find_transaction(self, txid: str) -> dict[str, Any] | None:
        for block in self.chain:
            for tx in block.transactions:
                if tx.txid() == txid:
                    return {"block": block.index, "transaction": tx.to_dict()}
        for tx in self.mempool:
            if tx.txid() == txid:
                return {"block": None, "transaction": tx.to_dict()}
        return None

    def tokens(self) -> dict[str, dict[str, Any]]:
        tokens: dict[str, dict[str, Any]] = {}
        for block in self.chain:
            for tx in block.transactions:
                if tx.tx_type == "token_create":
                    token_id = token_id_from_txid(tx.txid())
                    tokens[token_id] = {
                        "token_id": token_id,
                        "name": tx.data["name"],
                        "symbol": str(tx.data["symbol"]).upper(),
                        "decimals": int(tx.data.get("decimals", 0)),
                        "total_supply": float(tx.data["total_supply"]),
                        "issuer": tx.sender,
                        "created_in_block": block.index,
                        "create_txid": tx.txid(),
                    }
        return tokens

    def token_balance_of(self, token_id: str, address: str, include_mempool: bool = False) -> float:
        balance = 0.0
        transactions = []
        for block in self.chain:
            transactions.extend(block.transactions)
        if include_mempool:
            transactions.extend(self.mempool)
        for tx in transactions:
            if tx.tx_type == "token_create" and token_id_from_txid(tx.txid()) == token_id and tx.sender == address:
                balance += float(tx.data["total_supply"])
            if tx.tx_type == "token_transfer" and tx.data.get("token_id") == token_id:
                amount = float(tx.data.get("amount", 0))
                if tx.sender == address:
                    balance -= amount
                if tx.data.get("to") == address:
                    balance += amount
        return round(balance, COIN.decimals)

    def token_balances(self, address: str) -> dict[str, float]:
        balances = {}
        for token_id in self.tokens():
            balance = self.token_balance_of(token_id, address)
            if balance:
                balances[token_id] = balance
        return balances

    def contracts(self) -> dict[str, dict[str, Any]]:
        contracts: dict[str, dict[str, Any]] = {}
        for block in self.chain:
            for tx in block.transactions:
                if tx.tx_type == "contract_deploy":
                    contract_id = contract_id_from_txid(tx.txid())
                    contracts[contract_id] = {
                        "contract_id": contract_id,
                        "name": tx.data["name"],
                        "contract_type": tx.data["contract_type"],
                        "owner": tx.sender,
                        "created_in_block": block.index,
                        "deploy_txid": tx.txid(),
                    }
        return contracts

    def contract_state(self, contract_id: str) -> dict[str, Any] | None:
        contract = self.contracts().get(contract_id)
        if contract is None:
            return None
        state: dict[str, Any]
        if contract["contract_type"] == "counter":
            state = {"value": 0}
        else:
            state = {"values": {}}
        for block in self.chain:
            for tx in block.transactions:
                if tx.tx_type == "contract_call" and tx.data.get("contract_id") == contract_id:
                    self._apply_contract_call(contract["contract_type"], state, tx)
        return {"contract": contract, "state": state}

    def _apply_contract_call(self, contract_type: str, state: dict[str, Any], tx: Transaction) -> None:
        method = tx.data.get("method")
        args = tx.data.get("args", {})
        if contract_type == "counter":
            if method == "increment":
                state["value"] += int(args.get("amount", 1))
            elif method == "decrement":
                state["value"] -= int(args.get("amount", 1))
            elif method == "reset":
                state["value"] = 0
        if contract_type == "key_value":
            key = str(args.get("key", ""))
            if method == "set" and key:
                state["values"][key] = args.get("value")
            elif method == "delete" and key:
                state["values"].pop(key, None)

    def stats(self) -> dict[str, Any]:
        return {
            "coin": COIN.__dict__,
            "height": self.height(),
            "latest_hash": self.last_block.hash(),
            "consensus": "poa-round-robin",
            "validators": self.validators,
            "next_validator": self.next_validator(),
            "mempool_size": len(self.mempool),
            "supply": self.supply(),
            "token_count": len(self.tokens()),
            "contract_count": len(self.contracts()),
        }


def token_id_from_txid(txid: str) -> str:
    return "NOT" + txid[:37]


def contract_id_from_txid(txid: str) -> str:
    return "NOC" + txid[:37]
