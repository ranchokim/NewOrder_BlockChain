CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

TARGET = neworder_c
SRC = src/main.c src/neworder.c
INC = -Iinclude

all: $(TARGET)

$(TARGET): $(SRC) include/neworder.h
	$(CC) $(CFLAGS) $(INC) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) $(TARGET).exe

.PHONY: all clean
