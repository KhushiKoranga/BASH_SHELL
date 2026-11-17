# Advanced UNIX Job Control Shell
CC      = gcc
CFLAGS  = -std=gnu11 -Wall -Wextra -pedantic -g -Iinclude
SRC_DIR = src
OBJ_DIR = build
TARGET  = myshell

SRCS     = $(wildcard $(SRC_DIR)/*.c)
OBJS     = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
LIB_OBJS = $(filter-out $(OBJ_DIR)/main.o,$(OBJS))   # everything except main()
HEADERS  = $(wildcard include/*.h)

TESTS    = $(OBJ_DIR)/test_jobs $(OBJ_DIR)/test_execution

.PHONY: all tests check clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# unit tests link the shell modules (without main.o) with their own main()
$(OBJ_DIR)/test_%: tests/test_%.c $(LIB_OBJS) $(HEADERS) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIB_OBJS)

tests: $(TESTS)

check: all tests
	./$(OBJ_DIR)/test_jobs
	./$(OBJ_DIR)/test_execution
	sh tests/test_redirection.sh ./$(TARGET)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
