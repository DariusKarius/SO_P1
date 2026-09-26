CC      = cc
CFLAGS  = -std=gnu11 -Wall -Wextra -g -O0 -Iinclude -D_XOPEN_SOURCE=700 -Wno-deprecated-declarations
SRC     = src/thread.c src/mutex.c src/cond.c
OBJ     = $(SRC:.c=.o)

BIN_DIR = bin

.PHONY: all clean test

all: $(BIN_DIR)/test1_monitor $(BIN_DIR)/test2_inversion $(BIN_DIR)/test3_perverted $(BIN_DIR)/test4_fakecall

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

src/%.o: src/%.c src/mpt.h src/mpt_internal.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/test1_monitor: tests/test1_monitor.c $(OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $< $(OBJ) -o $@

$(BIN_DIR)/test2_inversion: tests/test2_inversion.c $(OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $< $(OBJ) -o $@

$(BIN_DIR)/test3_perverted: tests/test3_perverted.c $(OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $< $(OBJ) -o $@

$(BIN_DIR)/test4_fakecall: tests/test4_fakecall.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf $(BIN_DIR) src/*.o
