CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
INC = -Iinclude

# On Linux/macOS link with -lm; Winsock is handled inside the source via #pragma comment
UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -lm
endif
ifeq ($(UNAME_S),Darwin)
    LDFLAGS += -lm
endif

TARGETS = neworder_c neworder_node neworder_wallet

all: $(TARGETS)

# smoke-test + benchmark binary
neworder_c: src/main.c src/neworder.c src/sha256.c include/neworder.h include/sha256.h
	$(CC) $(CFLAGS) $(INC) -o $@ src/main.c src/neworder.c src/sha256.c $(LDFLAGS)

# HTTP node server
neworder_node: src/node_main.c src/neworder.c src/nohttp.c src/sha256.c \
               include/neworder.h include/sha256.h include/nohttp.h
	$(CC) $(CFLAGS) $(INC) -o $@ \
	    src/node_main.c src/neworder.c src/nohttp.c src/sha256.c $(LDFLAGS)

# Wallet CLI
neworder_wallet: src/wallet_main.c src/sha256.c include/neworder.h include/sha256.h
	$(CC) $(CFLAGS) $(INC) -o $@ src/wallet_main.c src/sha256.c $(LDFLAGS)

clean:
	rm -f $(TARGETS) $(addsuffix .exe,$(TARGETS)) *.o

.PHONY: all clean
