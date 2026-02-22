ROOT_DIR ?= ..

CC := x86_64-elf-gcc
CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -mcmodel=kernel
LDFLAGS ?= -nostdlib -ffreestanding -fno-pie -Wl,-T,$(ROOT_DIR)/userland/linker.ld -Wl,-z,max-page-size=0x1000

SRC := $(shell find src -type f -name '*.c')
OBJ := $(patsubst src/%.c,build/%.o,$(SRC))
DEP := $(OBJ:.o=.d)
BIN ?= $(ROOT_DIR)/userland/bin/starlight

INCLUDES := -I$(ROOT_DIR)/userland/include -Isrc -Isrc/render -Ithird_party -Ithird_party/stb

all: $(BIN)

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BIN): $(OBJ) $(ROOT_DIR)/userland/crt0.o $(ROOT_DIR)/userland/libpeos.a $(ROOT_DIR)/userland/linker.ld
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $(ROOT_DIR)/userland/crt0.o $(OBJ) $(ROOT_DIR)/userland/libpeos.a -o $@

clean:
	rm -rf build
	rm -f $(BIN)

.PHONY: all clean

-include $(DEP)
