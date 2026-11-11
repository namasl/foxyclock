# Host-side unit tests for the hardware-independent core component.
#   make -C test          build and run all tests
CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -Werror -g -O1 -D_DEFAULT_SOURCE
# Optional: make -C test SAN="-fsanitize=address,undefined" (needs libasan/libubsan)
CFLAGS  += $(SAN)
CFLAGS  += -I../core/include -Icjson -I.
BUILD   := build
CORE    := $(wildcard ../core/*.c)
TESTS   := $(patsubst %.c,$(BUILD)/%,$(wildcard test_*.c))

.PHONY: all clean
all: $(TESTS)
	@set -e; for t in $(TESTS); do ./$$t; done

$(BUILD)/cJSON.o: cjson/cJSON.c | $(BUILD)
	$(CC) -std=c99 -O1 -g $(SAN) -Icjson -c $< -o $@

$(BUILD)/test_%: test_%.c $(CORE) $(BUILD)/cJSON.o minitest.h $(wildcard ../core/include/*.h) | $(BUILD)
	$(CC) $(CFLAGS) $< $(CORE) $(BUILD)/cJSON.o -lm -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)
