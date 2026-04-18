# Host test build for nicola_engine.
#
# Compiles the engine source against stub Zephyr/ZMK headers, links with the
# mock runtime, and runs the test binary.
#
# Usage:
#   make            # build and run
#   make build      # build only
#   make clean

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wno-unused-parameter -g -O0
ROOT    := ../..

INCLUDES := \
  -I stubs \
  -I $(ROOT)/include

SRCS := \
  $(ROOT)/behaviors/chordis_engine.c \
  mock_runtime.c \
  test_runner.c

BIN := test_runner

.PHONY: all build run clean

all: run

build: $(BIN)

$(BIN): $(SRCS) mock_runtime.h stubs/zephyr/kernel.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(SRCS)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
