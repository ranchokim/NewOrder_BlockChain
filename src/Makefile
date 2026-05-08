CC     ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
INC     = -Iinclude
LDFLAGS = -lm

TARGETS = neworder_c neworder_node neworder_wallet

all: $(TARGETS)

neworder_c: src/main.c src/neworder.c src/sha256.c \
            include/neworder.h include/sha256.h
	$(CC) $(CFLAGS) $(INC) -o $@ \
	    src/main.c src/neworder.c src/sha256.c $(LDFLAGS)

neworder_node: src/node_main.c src/neworder.c src/nohttp.c src/sha256.c \
               include/neworder.h include/sha256.h include/nohttp.h
	$(CC) $(CFLAGS) $(INC) -o $@ \
	    src/node_main.c src/neworder.c src/nohttp.c src/sha256.c $(LDFLAGS)

neworder_wallet: src/wallet_main.c src/sha256.c \
                 include/neworder.h include/sha256.h
	$(CC) $(CFLAGS) $(INC) -o $@ \
	    src/wallet_main.c src/sha256.c $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o

.PHONY: all clean
