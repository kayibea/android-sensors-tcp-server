ANDROID_HOME := $(HOME)/Android/Sdk
NDK_VERSION  := $(shell ls -1 $(ANDROID_HOME)/ndk | sort -V | tail -n1)
NDK_ROOT     := $(ANDROID_HOME)/ndk/$(NDK_VERSION)
TOOLCHAIN    := $(NDK_ROOT)/toolchains/llvm/prebuilt/linux-x86_64
SYSROOT      := $(TOOLCHAIN)/sysroot
TARGET_ARCH  := x86_64
# TARGET_ARCH  := aarch64
API_LEVEL    := 33
TARGET       := $(TARGET_ARCH)-linux-android$(API_LEVEL)

CC := $(TOOLCHAIN)/bin/$(TARGET)-clang
CFLAGS := -std=c99 -Wall -Wextra -Werror -O2 --sysroot=$(SYSROOT)
LDFLAGS := -landroid

SRC=main.c
BIN=sensors-server

.PHONY: all clean push-file

all: $(BIN) push-file

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

push-file: $(BIN)
	scp $(BIN) tablet-emulator:tmp

clean:
	rm -f $(BIN)

