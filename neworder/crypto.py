"""Small cryptographic helpers built on Python's standard library."""

from __future__ import annotations

import hashlib
import math
import secrets

PUBLIC_EXPONENT = 65537


def sha256_hex(data: bytes | str) -> str:
    if isinstance(data, str):
        data = data.encode("utf-8")
    return hashlib.sha256(data).hexdigest()


def _is_probable_prime(value: int, rounds: int = 16) -> bool:
    if value < 2:
        return False
    small_primes = (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37)
    if value in small_primes:
        return True
    if any(value % prime == 0 for prime in small_primes):
        return False

    odd_part = value - 1
    shifts = 0
    while odd_part % 2 == 0:
        shifts += 1
        odd_part //= 2

    for _ in range(rounds):
        base = secrets.randbelow(value - 3) + 2
        witness = pow(base, odd_part, value)
        if witness in (1, value - 1):
            continue
        for _ in range(shifts - 1):
            witness = pow(witness, 2, value)
            if witness == value - 1:
                break
        else:
            return False
    return True


def _random_prime(bits: int) -> int:
    while True:
        candidate = secrets.randbits(bits) | (1 << (bits - 1)) | 1
        if _is_probable_prime(candidate):
            return candidate


def new_private_key() -> str:
    while True:
        p = _random_prime(256)
        q = _random_prime(256)
        if p == q:
            continue
        phi = (p - 1) * (q - 1)
        if math.gcd(PUBLIC_EXPONENT, phi) == 1:
            modulus = p * q
            private_exponent = pow(PUBLIC_EXPONENT, -1, phi)
            return f"rsa:{private_exponent:x}:{modulus:x}"


def _parse_key(key: str) -> tuple[int, int]:
    kind, exponent, modulus = key.split(":", 2)
    if kind != "rsa":
        raise ValueError("unsupported key format")
    return int(exponent, 16), int(modulus, 16)


def public_key_from_private(private_key: str) -> str:
    _, modulus = _parse_key(private_key)
    return f"rsa:{PUBLIC_EXPONENT:x}:{modulus:x}"


def address_from_public(public_key: str) -> str:
    return "NO" + sha256_hex(f"neworder-address:{public_key}")[:38]


def sign(private_key: str, message: str) -> str:
    private_exponent, modulus = _parse_key(private_key)
    digest = int(sha256_hex(message), 16) % modulus
    return f"{pow(digest, private_exponent, modulus):x}"


def verify_signature(public_key: str, message: str, signature: str) -> bool:
    try:
        public_exponent, modulus = _parse_key(public_key)
        digest = int(sha256_hex(message), 16) % modulus
        signed = int(signature, 16)
    except (TypeError, ValueError):
        return False
    return pow(signed, public_exponent, modulus) == digest
