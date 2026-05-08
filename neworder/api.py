"""HTTP explorer and node API for NewOrder."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any
from urllib.parse import parse_qs, urlparse

from .ai_payment import AIPaymentGateway
from .blockchain import Blockchain
from .crypto import address_from_public, public_key_from_private
from .models import Transaction
from .p2p import P2PNode


class NewOrderAPI(BaseHTTPRequestHandler):
    blockchain: Blockchain
    node: P2PNode
    ai_gateway: AIPaymentGateway
    validator_private_key: str | None = None

    def log_message(self, format: str, *args: Any) -> None:
        return

    def read_json(self) -> dict[str, Any]:
        length = int(self.headers.get("Content-Length", "0"))
        if length == 0:
            return {}
        return json.loads(self.rfile.read(length).decode("utf-8"))

    def send_json(self, status: int, payload: dict[str, Any] | list[Any]) -> None:
        body = json.dumps(payload, indent=2).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        parts = [part for part in parsed.path.split("/") if part]
        query = parse_qs(parsed.query)

        if parsed.path == "/":
            self.send_json(HTTPStatus.OK, {"service": "NewOrder Explorer API", "routes": ROUTES})
            return
        if parsed.path == "/stats":
            self.send_json(HTTPStatus.OK, self.blockchain.stats())
            return
        if parsed.path == "/chain":
            self.send_json(HTTPStatus.OK, [block.to_dict() for block in self.blockchain.chain])
            return
        if parsed.path == "/mempool":
            self.send_json(HTTPStatus.OK, [tx.to_dict() for tx in self.blockchain.mempool])
            return
        if parsed.path == "/peers":
            self.send_json(HTTPStatus.OK, {"self": self.node.address, "peers": self.node.peerbook.to_list()})
            return
        if parsed.path == "/tokens":
            self.send_json(HTTPStatus.OK, list(self.blockchain.tokens().values()))
            return
        if parsed.path == "/contracts":
            self.send_json(HTTPStatus.OK, list(self.blockchain.contracts().values()))
            return
        if parsed.path == "/ai/services":
            self.send_json(HTTPStatus.OK, self.ai_gateway.list_services())
            return
        if len(parts) == 3 and parts[0] == "ai" and parts[1] == "payments":
            payment = self.ai_gateway.get_payment(parts[2])
            if payment is None:
                self.send_json(HTTPStatus.NOT_FOUND, {"error": "payment not found"})
            else:
                self.send_json(HTTPStatus.OK, payment.to_dict())
            return
        if len(parts) == 2 and parts[0] == "blocks":
            block = self._block(parts[1])
            if block is None:
                self.send_json(HTTPStatus.NOT_FOUND, {"error": "block not found"})
            else:
                self.send_json(HTTPStatus.OK, block.to_dict())
            return
        if len(parts) == 2 and parts[0] == "tx":
            tx = self.blockchain.find_transaction(parts[1])
            if tx is None:
                self.send_json(HTTPStatus.NOT_FOUND, {"error": "transaction not found"})
            else:
                self.send_json(HTTPStatus.OK, tx)
            return
        if len(parts) == 2 and parts[0] == "address":
            address = parts[1]
            self.send_json(
                HTTPStatus.OK,
                {
                    "address": address,
                    "balance": self.blockchain.balance_of(address),
                    "tokens": self.blockchain.token_balances(address),
                },
            )
            return
        if len(parts) == 2 and parts[0] == "tokens":
            token = self.blockchain.tokens().get(parts[1])
            if token is None:
                self.send_json(HTTPStatus.NOT_FOUND, {"error": "token not found"})
            else:
                self.send_json(HTTPStatus.OK, token)
            return
        if len(parts) == 4 and parts[0] == "tokens" and parts[2] == "balances":
            token_id = parts[1]
            if token_id not in self.blockchain.tokens():
                self.send_json(HTTPStatus.NOT_FOUND, {"error": "token not found"})
            else:
                self.send_json(
                    HTTPStatus.OK,
                    {
                        "token_id": token_id,
                        "address": parts[3],
                        "balance": self.blockchain.token_balance_of(token_id, parts[3]),
                    },
                )
            return
        if len(parts) == 2 and parts[0] == "contracts":
            contract = self.blockchain.contract_state(parts[1])
            if contract is None:
                self.send_json(HTTPStatus.NOT_FOUND, {"error": "contract not found"})
            else:
                self.send_json(HTTPStatus.OK, contract)
            return
        if parsed.path == "/mine":
            reward_address = query.get("address", [""])[0] or None
            if not self.validator_private_key:
                self.send_json(HTTPStatus.BAD_REQUEST, {"error": "node has no validator wallet configured"})
                return
            try:
                block = self.blockchain.produce_pending(self.validator_private_key, reward_address)
            except ValueError as exc:
                self.send_json(
                    HTTPStatus.CONFLICT,
                    {
                        "error": str(exc),
                        "next_validator": self.blockchain.next_validator(),
                    },
                )
                return
            broadcasts = self.node.broadcast({"type": "block", "block": block.to_dict()})
            self.send_json(HTTPStatus.OK, {"block": block.to_dict(), "broadcasts": broadcasts})
            return

        self.send_json(HTTPStatus.NOT_FOUND, {"error": "route not found"})

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path == "/transactions":
            payload = self.read_json()
            tx = Transaction.from_dict(payload)
            ok, result = self.blockchain.add_transaction(tx)
            if ok:
                broadcasts = self.node.broadcast({"type": "transaction", "transaction": tx.to_dict()})
                self.send_json(HTTPStatus.CREATED, {"txid": result, "broadcasts": broadcasts})
            else:
                self.send_json(HTTPStatus.BAD_REQUEST, {"error": result})
            return
        if parsed.path == "/ai/payments":
            payload = self.read_json()
            try:
                payment = self.ai_gateway.create_payment(
                    service_id=str(payload.get("service_id", "")),
                    customer_address=str(payload.get("customer_address", "")),
                    units=int(payload.get("units", 1)),
                )
                self.send_json(HTTPStatus.CREATED, payment.to_dict())
            except (TypeError, ValueError) as exc:
                self.send_json(HTTPStatus.BAD_REQUEST, {"error": str(exc)})
            return
        if parsed.path.startswith("/ai/payments/"):
            parts = [part for part in parsed.path.split("/") if part]
            if len(parts) == 3 and parts[2] == "verify":
                self.send_json(HTTPStatus.BAD_REQUEST, {"error": "payment id is required"})
                return
            if len(parts) == 4 and parts[0] == "ai" and parts[1] == "payments":
                payment_id = parts[2]
                try:
                    if parts[3] == "verify":
                        payment = self.ai_gateway.verify_payment(payment_id)
                        self.send_json(HTTPStatus.OK, payment.to_dict())
                        return
                    if parts[3] == "consume":
                        payload = self.read_json()
                        result = self.ai_gateway.consume(payment_id, prompt=str(payload.get("prompt", "")))
                        self.send_json(HTTPStatus.OK, result)
                        return
                except ValueError as exc:
                    status = HTTPStatus.NOT_FOUND if str(exc) == "payment not found" else HTTPStatus.BAD_REQUEST
                    self.send_json(status, {"error": str(exc)})
                    return
        if parsed.path == "/peers":
            payload = self.read_json()
            peer = payload.get("peer")
            if not peer:
                self.send_json(HTTPStatus.BAD_REQUEST, {"error": "peer is required"})
                return
            try:
                response = self.node.connect(peer)
                self.send_json(HTTPStatus.OK, {"peer": peer, "response": response})
            except Exception as exc:
                self.send_json(HTTPStatus.BAD_GATEWAY, {"error": str(exc)})
            return
        self.send_json(HTTPStatus.NOT_FOUND, {"error": "route not found"})

    def _block(self, value: str):
        if value.isdigit():
            index = int(value)
            if 0 <= index < len(self.blockchain.chain):
                return self.blockchain.chain[index]
            return None
        for block in self.blockchain.chain:
            if block.hash() == value:
                return block
        return None


ROUTES = [
    "GET /stats",
    "GET /chain",
    "GET /blocks/{height-or-hash}",
    "GET /tx/{txid}",
    "GET /address/{address}",
    "GET /mempool",
    "GET /tokens",
    "GET /tokens/{token_id}",
    "GET /tokens/{token_id}/balances/{address}",
    "GET /contracts",
    "GET /contracts/{contract_id}",
    "GET /ai/services",
    "GET /ai/payments/{payment_id}",
    "GET /mine?address={NO...}",
    "GET /peers",
    "POST /transactions",
    "POST /ai/payments",
    "POST /ai/payments/{payment_id}/verify",
    "POST /ai/payments/{payment_id}/consume",
    "POST /peers",
]


def build_server(host: str, port: int, data_dir: str, p2p_host: str, p2p_port: int) -> ThreadingHTTPServer:
    return build_server_with_consensus(host, port, data_dir, p2p_host, p2p_port)


def build_server_with_consensus(
    host: str,
    port: int,
    data_dir: str,
    p2p_host: str,
    p2p_port: int,
    validators: list[str] | None = None,
    validator_private_key: str | None = None,
    ai_merchant_address: str | None = None,
) -> ThreadingHTTPServer:
    blockchain = Blockchain(data_dir, validators=validators)
    node = P2PNode(blockchain, p2p_host, p2p_port)
    node.start()
    merchant_address = ai_merchant_address or validator_address_from_private_key(validator_private_key) or "NO_AI_MERCHANT"
    ai_gateway = AIPaymentGateway(blockchain, merchant_address, data_dir)

    class Handler(NewOrderAPI):
        pass

    Handler.blockchain = blockchain
    Handler.node = node
    Handler.ai_gateway = ai_gateway
    Handler.validator_private_key = validator_private_key
    return ThreadingHTTPServer((host, port), Handler)


def load_private_key(wallet_path: str | None) -> str | None:
    if not wallet_path:
        return None
    with Path(wallet_path).open("r", encoding="utf-8") as fh:
        return json.load(fh)["private_key"]


def validator_address_from_private_key(private_key: str | None) -> str | None:
    if not private_key:
        return None
    return address_from_public(public_key_from_private(private_key))


def main() -> None:
    parser = argparse.ArgumentParser(description="Run a NewOrder node with explorer API.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--p2p-host", default="127.0.0.1")
    parser.add_argument("--p2p-port", type=int, default=9333)
    parser.add_argument("--data-dir", default="data")
    parser.add_argument("--validator-wallet", help="Wallet JSON used to sign PoA blocks.")
    parser.add_argument("--validators", default="", help="Comma-separated validator NO addresses in round-robin order.")
    parser.add_argument("--ai-merchant-address", help="NO address that receives AI service payments.")
    args = parser.parse_args()

    validators = [item.strip() for item in args.validators.split(",") if item.strip()]
    validator_private_key = load_private_key(args.validator_wallet)
    server = build_server_with_consensus(
        args.host,
        args.port,
        args.data_dir,
        args.p2p_host,
        args.p2p_port,
        validators=validators,
        validator_private_key=validator_private_key,
        ai_merchant_address=args.ai_merchant_address,
    )
    print(f"NewOrder API running at http://{args.host}:{args.port}")
    print(f"NewOrder P2P node listening at {args.p2p_host}:{args.p2p_port}")
    print(f"NewOrder consensus: PoA round-robin validators={len(validators)}")
    server.serve_forever()


if __name__ == "__main__":
    main()
