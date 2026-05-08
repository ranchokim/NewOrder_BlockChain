import unittest
import uuid
from pathlib import Path

from neworder.ai_payment import AIPaymentGateway
from neworder.blockchain import Blockchain
from neworder.crypto import address_from_public, new_private_key, public_key_from_private
from neworder.models import Transaction


TEST_TMP_ROOT = Path(__file__).resolve().parents[1] / "tmp_tests"


def make_test_dir() -> Path:
    path = TEST_TMP_ROOT / f"ai_payment_{uuid.uuid4().hex}"
    path.mkdir(parents=True)
    return path


def address_for(private_key: str) -> str:
    return address_from_public(public_key_from_private(private_key))


class AIPaymentGatewayTest(unittest.TestCase):
    def test_paid_chain_transfer_unlocks_ai_consumption(self) -> None:
        TEST_TMP_ROOT.mkdir(exist_ok=True)
        data_dir = make_test_dir()
        validator_key = new_private_key()
        customer_key = new_private_key()
        merchant_key = new_private_key()
        validator_address = address_for(validator_key)
        customer_address = address_for(customer_key)
        merchant_address = address_for(merchant_key)
        chain = Blockchain(data_dir, validators=[validator_address])
        gateway = AIPaymentGateway(chain, merchant_address, data_dir)

        chain.produce_pending(validator_key, reward_address=customer_address)
        payment = gateway.create_payment("chat-basic", customer_address, units=2)

        tx = Transaction.signed(
            private_key=customer_key,
            recipient=payment.merchant_address,
            amount=payment.amount_due,
            memo=payment.memo,
        )
        ok, reason = chain.add_transaction(tx)
        self.assertTrue(ok, reason)
        chain.produce_pending(validator_key)

        verified = gateway.verify_payment(payment.payment_id)
        self.assertEqual("paid", verified.status)
        self.assertEqual(tx.txid(), verified.paid_txid)

        first = gateway.consume(payment.payment_id, prompt="hello")
        second = gateway.consume(payment.payment_id, prompt="again")
        self.assertEqual("chat", first["result"]["type"])
        self.assertEqual("consumed", second["payment"]["status"])

        with self.assertRaises(ValueError):
            gateway.consume(payment.payment_id, prompt="extra")

    def test_unpaid_payment_cannot_be_consumed(self) -> None:
        TEST_TMP_ROOT.mkdir(exist_ok=True)
        data_dir = make_test_dir()
        customer_key = new_private_key()
        merchant_key = new_private_key()
        customer_address = address_for(customer_key)
        chain = Blockchain(data_dir)
        gateway = AIPaymentGateway(chain, address_for(merchant_key), data_dir)
        payment = gateway.create_payment("summary", customer_address, units=1)

        verified = gateway.verify_payment(payment.payment_id)
        self.assertEqual("pending", verified.status)
        with self.assertRaises(ValueError):
            gateway.consume(payment.payment_id, prompt="not paid")


if __name__ == "__main__":
    unittest.main()
