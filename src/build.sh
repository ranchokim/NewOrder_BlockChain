#!/bin/sh
set -e

CC="${CC:-cc}"
CFLAGS="-O2 -Wall -Wextra -std=c11"
INC="-Iinclude"
LDFLAGS="-lm"

build() {
    out="$1"; shift
    echo "Building $out..."
    $CC $CFLAGS $INC -o "$out" "$@" $LDFLAGS
    echo "  -> $out OK"
}

build neworder_c      src/main.c src/neworder.c src/sha256.c
build neworder_node   src/node_main.c src/neworder.c src/nohttp.c src/sha256.c
build neworder_wallet src/wallet_main.c src/sha256.c

echo ""
echo "All binaries built: neworder_c  neworder_node  neworder_wallet"
