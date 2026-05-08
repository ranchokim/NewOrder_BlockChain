"""Coin constants for NewOrder."""

from dataclasses import dataclass


@dataclass(frozen=True)
class CoinSpec:
    name: str = "NewOrder"
    symbol: str = "NO"
    decimals: int = 8
    max_supply: int = 21_000_000
    block_reward: int = 50
    target_block_seconds: int = 30
    consensus: str = "Proof of Authority"
    genesis_message: str = "NewOrder genesis block"


COIN = CoinSpec()
