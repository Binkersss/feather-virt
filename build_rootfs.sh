#!/usr/bin/env bash
set -euo pipefail

BASE_DIR="/var/sandbox/basefs"
DISTRO="alpine"
VERSION="3.20.2"
TARGET="$BASE_DIR/$DISTRO-$VERSION"

mkdir -p "$BASE_DIR"

echo "[+] Building minimal $DISTRO rootfs at $TARGET"

if [ -d "$TARGET" ]; then
    echo "[!] $TARGET already exists. Delete it to rebuild."
    exit 0
fi

mkdir -p "$TARGET"

# Step 1: Fetch and extract a minimal rootfs tarball
curl -L "https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86_64/alpine-minirootfs-$VERSION-x86_64.tar.gz" \
    -o /tmp/alpine-rootfs.tar.gz

tar -xzf /tmp/alpine-rootfs.tar.gz -C "$TARGET"

# Step 2: Remove unneeded parts (optional)
rm -rf "$TARGET"/{var/cache,usr/share/man,usr/share/doc}

# Step 3: Create minimal device stubs
mkdir -p "$TARGET"/dev
sudo mknod -m 666 "$TARGET"/dev/null c 1 3
sudo mknod -m 666 "$TARGET"/dev/zero c 1 5

# Step 4: Add basic DNS & environment
echo "nameserver 1.1.1.1" > "$TARGET/etc/resolv.conf"
echo "sandbox" > "$TARGET/etc/hostname"

# Step 5: Optional: compress for distribution
tar -czf "$TARGET.tar.gz" -C "$BASE_DIR" "$(basename "$TARGET")"

echo "[+] Done. RootFS ready at $TARGET"

