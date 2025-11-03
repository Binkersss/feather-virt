CC = gcc
CFLAGS = -Wall -Wextra -O2 -D_GNU_SOURCE
LDFLAGS =

TARGET = feather_virt_dev
SRCS = main.c config.c overlay.c cgroup.c namespace.c
OBJS = $(SRCS:.c=.o)
HEADERS = config.h overlay.h cgroup.h namespace.h

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(TARGET) $(OBJS)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

# Development helpers
.PHONY: setup-dirs test-alpine

setup-dirs:
	@echo "Creating sandbox directory structure..."
	mkdir -p /var/sandbox/basefs
	mkdir -p /var/sandbox/containers
	mkdir -p /sys/fs/cgroup/sandbox

test-alpine: $(TARGET)
	./$(TARGET) --image alpine-3.20.2 --name test1
