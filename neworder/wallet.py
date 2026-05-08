"""Command-line wallet for NewOrder."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from .coin import COIN
from .crypto import address_from_public, new_private_key, public_key_from_private
from .models import Transaction


DEFAULT_WALLET = Path("wallet.json")


def load_wallet(path: Path) -> dict[str, Any]:
    if not path.exists():
        raise SystemExit(f"wallet not found: {path}. Run `python -m neworder.wallet create` first.")
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def save_wallet(path: Path, wallet: dict[str, Any]) -> None:
    with path.open("w", encoding="utf-8") as fh:
        json.dump(wallet, fh, indent=2)


def api_get(base_url: str, path: str) -> dict[str, Any] | list[Any]:
    with urlopen(base_url.rstrip("/") + path, timeout=10) as response:
        return json.loads(response.read().decode("utf-8"))


def api_post(base_url: str, path: str, payload: dict[str, Any]) -> dict[str, Any]:
    body = json.dumps(payload).encode("utf-8")
    request = Request(
        base_url.rstrip("/") + path,
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urlopen(request, timeout=10) as response:
            return json.loads(response.read().decode("utf-8"))
    except HTTPError as exc:
        detail = exc.read().decode("utf-8")
        raise SystemExit(f"node rejected request: HTTP {exc.code} {detail}") from exc


def create_wallet(args: argparse.Namespace) -> None:
    private_key = new_private_key()
    public_key = public_key_from_private(private_key)
    wallet = {
        "coin": COIN.name,
        "symbol": COIN.symbol,
        "address": address_from_public(public_key),
        "public_key": public_key,
        "private_key": private_key,
    }
    save_wallet(args.wallet, wallet)
    print(json.dumps({"address": wallet["address"], "wallet": str(args.wallet)}, indent=2))


def show_address(args: argparse.Namespace) -> None:
    wallet = load_wallet(args.wallet)
    print(wallet["address"])


def show_balance(args: argparse.Namespace) -> None:
    wallet = load_wallet(args.wallet)
    print(json.dumps(api_get(args.node, f"/address/{wallet['address']}"), indent=2))


def send_transaction(args: argparse.Namespace) -> None:
    wallet = load_wallet(args.wallet)
    tx = Transaction.signed(
        private_key=wallet["private_key"],
        recipient=args.to,
        amount=args.amount,
        fee=args.fee,
        memo=args.memo,
    )
    print(json.dumps(api_post(args.node, "/transactions", tx.to_dict()), indent=2))


def ai_services(args: argparse.Namespace) -> None:
    print(json.dumps(api_get(args.node, "/ai/services"), indent=2))


def ai_create_payment(args: argparse.Namespace) -> None:
    wallet = load_wallet(args.wallet)
    payload = {
        "service_id": args.service_id,
        "customer_address": wallet["address"],
        "units": args.units,
    }
    print(json.dumps(api_post(args.node, "/ai/payments", payload), indent=2))


def ai_pay(args: argparse.Namespace) -> None:
    wallet = load_wallet(args.wallet)
    payment = api_get(args.node, f"/ai/payments/{args.payment_id}")
    if not isinstance(payment, dict):
        raise SystemExit("unexpected payment response")
    tx = Transaction.signed(
        private_key=wallet["private_key"],
        recipient=str(payment["merchant_address"]),
        amount=float(payment["amount_due"]),
        fee=args.fee,
        memo=str(payment["memo"]),
    )
    print(json.dumps(api_post(args.node, "/transactions", tx.to_dict()), indent=2))


def ai_verify(args: argparse.Namespace) -> None:
    print(json.dumps(api_post(args.node, f"/ai/payments/{args.payment_id}/verify", {}), indent=2))


def ai_consume(args: argparse.Namespace) -> None:
    payload = {"prompt": args.prompt}
    print(json.dumps(api_post(args.node, f"/ai/payments/{args.payment_id}/consume", payload), indent=2))


def create_token(args: argparse.Namespace) -> None:
    wallet = load_wallet(args.wallet)
    tx = Transaction.signed(
        private_key=wallet["private_key"],
        recipient="TOKEN",
        amount=0,
        fee=args.fee,
        memo=args.memo,
        tx_type="token_create",
        data={
            "name": args.name,
            "symbol": args.symbol.upper(),
            "total_supply": args.supply,
            "decimals": args.decimals,
        },
    )
    print(json.dumps(api_post(args.node, "/transactions", tx.to_dict()), indent=2))


def send_token(args: argparse.Namespace) -> None:
    wallet = load_wallet(args.wallet)
    tx = Transaction.signed(
        private_key=wallet["private_key"],
        recipient=args.to,
        amount=0,
        fee=args.fee,
        memo=args.memo,
        tx_type="token_transfer",
        data={"token_id": args.token_id, "to": args.to, "amount": args.amount},
    )
    print(json.dumps(api_post(args.node, "/transactions", tx.to_dict()), indent=2))


def deploy_contract(args: argparse.Namespace) -> None:
    wallet = load_wallet(args.wallet)
    tx = Transaction.signed(
        private_key=wallet["private_key"],
        recipient="CONTRACT",
        amount=0,
        fee=args.fee,
        memo=args.memo,
        tx_type="contract_deploy",
        data={"name": args.name, "contract_type": args.contract_type},
    )
    print(json.dumps(api_post(args.node, "/transactions", tx.to_dict()), indent=2))


def call_contract(args: argparse.Namespace) -> None:
    wallet = load_wallet(args.wallet)
    try:
        call_args = json.loads(args.args_json)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"--args-json must be valid JSON: {exc}") from exc
    tx = Transaction.signed(
        private_key=wallet["private_key"],
        recipient=args.contract_id,
        amount=0,
        fee=args.fee,
        memo=args.memo,
        tx_type="contract_call",
        data={"contract_id": args.contract_id, "method": args.method, "args": call_args},
    )
    print(json.dumps(api_post(args.node, "/transactions", tx.to_dict()), indent=2))


def mine(args: argparse.Namespace) -> None:
    wallet = load_wallet(args.wallet)
    print(json.dumps(api_get(args.node, f"/mine?address={wallet['address']}"), indent=2))


def main() -> None:
    parser = argparse.ArgumentParser(description="NewOrder wallet CLI.")
    parser.add_argument("--wallet", type=Path, default=DEFAULT_WALLET)
    parser.add_argument("--node", default="http://127.0.0.1:8080")
    subparsers = parser.add_subparsers(dest="command", required=True)

    wallet_parent = argparse.ArgumentParser(add_help=False)
    wallet_parent.add_argument("--wallet", type=Path, default=DEFAULT_WALLET)

    node_parent = argparse.ArgumentParser(add_help=False)
    node_parent.add_argument("--node", default="http://127.0.0.1:8080")

    create = subparsers.add_parser("create", parents=[wallet_parent], help="Create a wallet file.")
    create.set_defaults(func=create_wallet)

    address = subparsers.add_parser("address", parents=[wallet_parent], help="Print wallet address.")
    address.set_defaults(func=show_address)

    balance = subparsers.add_parser("balance", parents=[wallet_parent, node_parent], help="Fetch wallet balance from a node.")
    balance.set_defaults(func=show_balance)

    send = subparsers.add_parser("send", parents=[wallet_parent, node_parent], help="Send NewOrder coins.")
    send.add_argument("--to", required=True)
    send.add_argument("--amount", type=float, required=True)
    send.add_argument("--fee", type=float, default=0.0)
    send.add_argument("--memo", default="")
    send.set_defaults(func=send_transaction)

    ai_list = subparsers.add_parser("ai-services", parents=[node_parent], help="List payable AI services.")
    ai_list.set_defaults(func=ai_services)

    ai_create = subparsers.add_parser("ai-payment-create", parents=[wallet_parent, node_parent], help="Create an AI payment request.")
    ai_create.add_argument("--service-id", required=True)
    ai_create.add_argument("--units", type=int, default=1)
    ai_create.set_defaults(func=ai_create_payment)

    ai_payment = subparsers.add_parser("ai-pay", parents=[wallet_parent, node_parent], help="Pay an AI payment request with NO.")
    ai_payment.add_argument("--payment-id", required=True)
    ai_payment.add_argument("--fee", type=float, default=0.0)
    ai_payment.set_defaults(func=ai_pay)

    ai_payment_verify = subparsers.add_parser("ai-verify", parents=[node_parent], help="Verify an AI payment on-chain.")
    ai_payment_verify.add_argument("--payment-id", required=True)
    ai_payment_verify.set_defaults(func=ai_verify)

    ai_payment_consume = subparsers.add_parser("ai-consume", parents=[node_parent], help="Use a paid AI service unit.")
    ai_payment_consume.add_argument("--payment-id", required=True)
    ai_payment_consume.add_argument("--prompt", default="")
    ai_payment_consume.set_defaults(func=ai_consume)

    token_create = subparsers.add_parser("token-create", parents=[wallet_parent, node_parent], help="Issue a NewOrder token.")
    token_create.add_argument("--name", required=True)
    token_create.add_argument("--symbol", required=True)
    token_create.add_argument("--supply", type=float, required=True)
    token_create.add_argument("--decimals", type=int, default=0)
    token_create.add_argument("--fee", type=float, default=0.0)
    token_create.add_argument("--memo", default="")
    token_create.set_defaults(func=create_token)

    token_send = subparsers.add_parser("token-send", parents=[wallet_parent, node_parent], help="Send an issued token.")
    token_send.add_argument("--token-id", required=True)
    token_send.add_argument("--to", required=True)
    token_send.add_argument("--amount", type=float, required=True)
    token_send.add_argument("--fee", type=float, default=0.0)
    token_send.add_argument("--memo", default="")
    token_send.set_defaults(func=send_token)

    contract_deploy = subparsers.add_parser("contract-deploy", parents=[wallet_parent, node_parent], help="Deploy a built-in smart contract.")
    contract_deploy.add_argument("--name", required=True)
    contract_deploy.add_argument("--contract-type", choices=["counter", "key_value"], required=True)
    contract_deploy.add_argument("--fee", type=float, default=0.0)
    contract_deploy.add_argument("--memo", default="")
    contract_deploy.set_defaults(func=deploy_contract)

    contract_call = subparsers.add_parser("contract-call", parents=[wallet_parent, node_parent], help="Call a built-in smart contract.")
    contract_call.add_argument("--contract-id", required=True)
    contract_call.add_argument("--method", required=True)
    contract_call.add_argument("--args-json", default="{}")
    contract_call.add_argument("--fee", type=float, default=0.0)
    contract_call.add_argument("--memo", default="")
    contract_call.set_defaults(func=call_contract)

    mine_parser = subparsers.add_parser("mine", parents=[wallet_parent, node_parent], help="Ask the validator node to produce a PoA block and pay this wallet.")
    mine_parser.set_defaults(func=mine)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
