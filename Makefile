GCC_COMPILER = gcc
GCC_FLAGS = -Wall -Wextra -O2 -D_GNU_SOURCE

ZIG_COMPILER = zig cc
ZIG_FLAGS = $(GCC_FLAGS)

CC ?= $(GCC_COMPILER)
CFLAGS = $(GCC_FLAGS)
LDFLAGS =

TARGET = feather_virt_dev
SRCS = main.c config.c overlay.c cgroup.c namespace.c
OBJS = $(SRCS:.c=.o)
HEADERS = config.h overlay.h cgroup.h namespace.h

.PHONY: all clean install zig-build

all: $(TARGET)	## Build the main executable (default)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $<

clean:	## Remove build artifacts
	rm -f $(TARGET) $(OBJS)

install: $(TARGET)	## Install binary to /usr/local/bin
	install -m 755 $(TARGET) /usr/local/bin/

zig-build:	## Build using Zig compiler
	@echo "Building with Zig compiler..."
	$(MAKE) CC="$(ZIG_COMPILER)" CFLAGS="$(ZIG_FLAGS)" LDFLAGS="$(LDFLAGS)" all 

# Development helpers
.PHONY: setup-dirs test-alpine help

setup-dirs:	  ## Create required sandbox directories
	@echo "Creating sandbox directory structure..."
	mkdir -p /var/sandbox/basefs
	mkdir -p /var/sandbox/containers
	mkdir -p /sys/fs/cgroup/sandbox

test-alpine: $(TARGET)	## Run Alpine test container
	./$(TARGET) --image alpine-3.20.2 --name test1

help:	## Show this help message
	@echo "Available make targets:"
	@grep -E '^[a-zA-Z0-9_-]+:.*?## ' $(MAKEFILE_LIST) | \
		sort | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[1;33m%-15s\033[0m %s\n", $$1, $$2}'
