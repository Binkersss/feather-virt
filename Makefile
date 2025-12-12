GCC_COMPILER = gcc
GCC_FLAGS = -Wall -Wextra -O2 -D_GNU_SOURCE

ZIG_COMPILER = zig cc
ZIG_FLAGS = $(GCC_FLAGS)

# detect for json-c with pkg-config
JSON_C_CFLAGS := $(shell pkg-config --cflags json-c 2>/dev/null)
JSON_C_LDFLAGS := $(shell pkg-config --libs json-c 2>/dev/null)

# Fallback for pkg-config
ifeq ($(JSON_C_CFLAGS),)
    JSON_C_CFLAGS = -I/usr/include/json-c
endif
ifeq ($(JSON_C_LDFLAGS),)
    JSON_C_LDFLAGS = -ljson-c
endif

CC ?= $(GCC_COMPILER)
CFLAGS = $(GCC_FLAGS) $(JSON_C_CFLAGS)
LDFLAGS = $(JSON_C_LDFLAGS)

TARGET = feather_virt_dev
SRCS = main.c config.c overlay.c cgroup.c namespace.c
OBJS = $(SRCS:.c=.o)
HEADERS = config.h overlay.h cgroup.h namespace.h

.PHONY: all clean install zig-build

all: check-deps $(TARGET)	## Build the main executable (default)

check-deps:  ## Check for required dependencies
	@echo "Checking dependencies..."
	@command -v pkg-config >/dev/null 2>&1 || { echo "Error: pkg-config not found"; exit 1; }
	@pkg-config --exists json-c 2>/dev/null || { \
		echo "Error: json-c library not found!"; \
		exit 1; \
	}
	@echo "✓ All dependencies found"
	@echo "  json-c CFLAGS: $(JSON_C_CFLAGS)"
	@echo "  json-c LDFLAGS: $(JSON_C_LDFLAGS)"

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
	$(MAKE) CC="$(ZIG_COMPILER)" CFLAGS="$(ZIG_FLAGS) $(JSON_C_CFLAGS)" LDFLAGS="$(LDFLAGS)" all 

# Development helpers
.PHONY: setup-dirs test-alpine formant help

setup-dirs:	  ## Create required sandbox directories
	@echo "Creating sandbox directory structure..."
	mkdir -p /var/sandbox/basefs
	mkdir -p /var/sandbox/containers
	mkdir -p /sys/fs/cgroup/sandbox

test-alpine: $(TARGET)	## Run Alpine test container
	./$(TARGET) --image alpine-3.20.2 --name test1

test-debug: $(TARGET)
	./$(TARGET) --image alpine-3.20.2 --name debug-test --debug
format: 	## Runs indent formatter with the Kernighan & Ritchie style 
	indent -kr *.c *.h

help:	## Show this help message
	@echo "Available make targets:"
	@grep -E '^[a-zA-Z0-9_-]+:.*?## ' $(MAKEFILE_LIST) | \
		sort | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[1;33m%-15s\033[0m %s\n", $$1, $$2}'
