"""HTTP explorer and node API for NewOrder."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any
from urllib.parse import parse_qs, urlparse

from .blockchain import Blockchain
from .models import Transaction
from .p2p import P2PNode


class NewOrderAPI(BaseHTTPRequestHandler):
    blockchain: Blockchain
    node: P2PNode
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
    "GET /mine?address={NO...}",
    "GET /peers",
    "POST /transactions",
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
) -> ThreadingHTTPServer:
    blockchain = Blockchain(data_dir, validators=validators)
    node = P2PNode(blockchain, p2p_host, p2p_port)
    node.start()

    class Handler(NewOrderAPI):
        pass

    Handler.blockchain = blockchain
    Handler.node = node
    Handler.validator_private_key = validator_private_key
    return ThreadingHTTPServer((host, port), Handler)


def load_private_key(wallet_path: str | None) -> str | None:
    if not wallet_path:
        return None
    with Path(wallet_path).open("r", encoding="utf-8") as fh:
        return json.load(fh)["private_key"]


def main() -> None:
    parser = argparse.ArgumentParser(description="Run a NewOrder node with explorer API.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--p2p-host", default="127.0.0.1")
    parser.add_argument("--p2p-port", type=int, default=9333)
    parser.add_argument("--data-dir", default="data")
    parser.add_argument("--validator-wallet", help="Wallet JSON used to sign PoA blocks.")
    parser.add_argument("--validators", default="", help="Comma-separated validator NO addresses in round-robin order.")
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
    )
    print(f"NewOrder API running at http://{args.host}:{args.port}")
    print(f"NewOrder P2P node listening at {args.p2p_host}:{args.p2p_port}")
    print(f"NewOrder consensus: PoA round-robin validators={len(validators)}")
    server.serve_forever()


if __name__ == "__main__":
    main()
