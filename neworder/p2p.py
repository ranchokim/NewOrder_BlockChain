"""JSON-line TCP peer networking for NewOrder nodes."""

from __future__ import annotations

import json
import socket
import socketserver
import threading
from dataclasses import dataclass, field
from typing import Any

from .blockchain import Blockchain
from .models import Block, Transaction


@dataclass
class PeerBook:
    peers: set[str] = field(default_factory=set)

    def add(self, peer: str) -> None:
        if ":" not in peer:
            raise ValueError("peer must be host:port")
        self.peers.add(peer)

    def to_list(self) -> list[str]:
        return sorted(self.peers)


class PeerRequestHandler(socketserver.StreamRequestHandler):
    def handle(self) -> None:
        raw = self.rfile.readline().decode("utf-8").strip()
        if not raw:
            return
        try:
            message = json.loads(raw)
            response = self.server.node.handle_message(message)
        except Exception as exc:  # pragma: no cover - defensive network boundary
            response = {"ok": False, "error": str(exc)}
        self.wfile.write((json.dumps(response) + "\n").encode("utf-8"))


class ThreadedPeerServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True

    def __init__(self, server_address: tuple[str, int], handler: type[PeerRequestHandler], node: "P2PNode"):
        self.node = node
        super().__init__(server_address, handler)


class P2PNode:
    def __init__(self, blockchain: Blockchain, host: str = "127.0.0.1", port: int = 9333):
        self.blockchain = blockchain
        self.host = host
        self.port = port
        self.peerbook = PeerBook()
        self.server: ThreadedPeerServer | None = None
        self.thread: threading.Thread | None = None

    @property
    def address(self) -> str:
        return f"{self.host}:{self.port}"

    def start(self) -> None:
        if self.server:
            return
        self.server = ThreadedPeerServer((self.host, self.port), PeerRequestHandler, self)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def stop(self) -> None:
        if self.server:
            self.server.shutdown()
            self.server.server_close()
            self.server = None

    def connect(self, peer: str) -> dict[str, Any]:
        self.peerbook.add(peer)
        response = self.send(peer, {"type": "hello", "peer": self.address})
        for remote_peer in response.get("peers", []):
            if remote_peer != self.address:
                self.peerbook.add(remote_peer)
        self.sync_from(peer)
        return response

    def send(self, peer: str, message: dict[str, Any], timeout: float = 5.0) -> dict[str, Any]:
        host, port = peer.split(":", 1)
        with socket.create_connection((host, int(port)), timeout=timeout) as sock:
            sock.sendall((json.dumps(message) + "\n").encode("utf-8"))
            with sock.makefile("r", encoding="utf-8") as fh:
                raw = fh.readline()
        return json.loads(raw) if raw else {"ok": False, "error": "empty peer response"}

    def broadcast(self, message: dict[str, Any]) -> list[dict[str, Any]]:
        responses = []
        for peer in self.peerbook.to_list():
            try:
                responses.append({"peer": peer, "response": self.send(peer, message)})
            except OSError as exc:
                responses.append({"peer": peer, "response": {"ok": False, "error": str(exc)}})
        return responses

    def sync_from(self, peer: str) -> bool:
        response = self.send(peer, {"type": "get_chain"})
        if not response.get("ok"):
            return False
        blocks = [Block.from_dict(block) for block in response.get("chain", [])]
        return self.blockchain.replace_chain(blocks)

    def handle_message(self, message: dict[str, Any]) -> dict[str, Any]:
        kind = message.get("type")
        if kind == "hello":
            peer = message.get("peer")
            if peer and peer != self.address:
                self.peerbook.add(peer)
            return {"ok": True, "height": self.blockchain.height(), "peers": self.peerbook.to_list()}
        if kind == "get_chain":
            return {"ok": True, "chain": [block.to_dict() for block in self.blockchain.chain]}
        if kind == "transaction":
            tx = Transaction.from_dict(message["transaction"])
            ok, result = self.blockchain.add_transaction(tx)
            return {"ok": ok, "result": result}
        if kind == "block":
            block = Block.from_dict(message["block"])
            ok, result = self.blockchain.add_block(block)
            return {"ok": ok, "result": result}
        if kind == "peers":
            return {"ok": True, "peers": self.peerbook.to_list()}
        return {"ok": False, "error": f"unknown message type: {kind}"}
