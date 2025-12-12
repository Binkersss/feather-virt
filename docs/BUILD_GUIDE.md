# Image Building Guide

## Overview

The `scripts/build_rootfs.sh` script builds minimal, compressed rootfs images for use with the container runtime. Images are stored as `.tar.gz` archives with metadata tracking via a JSON manifest.

## Quick Start

```bash
# Build all supported images
sudo scripts/build_rootfs.sh build-all

# Build specific image
sudo scripts/build_rootfs.sh build alpine 3.20.2

# List available images
sudo scripts/build_rootfs.sh list

# Verify all images
sudo scripts/build_rootfs.sh verify
```

## Supported Distributions

### Alpine Linux 3.20.2
- **Size**: ~8-10MB compressed
- **Features**: Full musl-based system with apk package manager
- **Shell**: `/bin/ash`
- **Use case**: General-purpose containers

```bash
sudo scripts/build_rootfs.sh build alpine 3.20.2
```

### BusyBox 1.35.0
- **Size**: ~2-3MB compressed
- **Features**: Minimal single-binary system
- **Shell**: `/bin/sh`
- **Use case**: Ultra-minimal containers, testing

```bash
sudo scripts/build_rootfs.sh build busybox 1.35.0
```

## Image Lifecycle

### 1. Building
The script downloads base images, minimizes them, and creates compressed archives:

```
Download → Extract → Minimize → Configure → Compress → Manifest
```

**Minimization includes:**
- Remove documentation (`/usr/share/man`, `/usr/share/doc`)
- Remove package caches (`/var/cache`)
- Create essential directories (`/dev`, `/proc`, `/sys`, `/tmp`)
- Add basic device nodes
- Configure networking (`/etc/resolv.conf`, `/etc/hostname`)

### 2. Storage
Built images are stored in `/var/sandbox/basefs/`:

```
/var/sandbox/basefs/
├── alpine-3.20.2.tar.gz
├── busybox-1.35.0.tar.gz
└── manifest.json
```

**Manifest format:**
```json
{
  "images": [
    {
      "distro": "alpine",
      "version": "3.20.2",
      "path": "/var/sandbox/basefs/alpine-3.20.2.tar.gz",
      "checksum": "sha256:abc123...",
      "size_bytes": 8623104,
      "size_mb": 8.23,
      "created": "2024-01-15T10:30:00Z"
    }
  ]
}
```

### 3. Runtime Extraction
When you first launch a container with an image:

```bash
sudo ./feather_virt --image alpine-3.20.2 --name test
```

The runtime:
1. Checks if `alpine-3.20.2.tar.gz` exists in `/var/sandbox/basefs/`
2. Checks if already extracted to `/var/sandbox/cache/alpine-3.20.2/`
3. If not cached, extracts: `tar -xzf alpine-3.20.2.tar.gz -C /var/sandbox/cache/`
4. Uses extracted directory as the overlay lowerdir

### 4. Caching
Extracted images are **reused** across all containers:

```
/var/sandbox/cache/alpine-3.20.2/  ← Read-only, shared by all containers
/var/sandbox/containers/
├── web1-12345/upper/               ← Container-specific changes
├── web1-12345/rootfs/              ← Merged view
├── web2-12346/upper/               ← Different container
└── web2-12346/rootfs/              ← Different merged view
```

## Integrity Verification

The manifest tracks SHA256 checksums for each image:

```bash
# Verify all images match their checksums
sudo scripts/build_rootfs.sh verify
```

Output:
```
[+] Verifying all images...
[+] Image integrity verified: /var/sandbox/basefs/alpine-3.20.2.tar.gz
[+] Image integrity verified: /var/sandbox/basefs/busybox-1.35.0.tar.gz
[+] All images verified successfully
```

## Manual Operations

### Force Rebuild
```bash
sudo scripts/build_rootfs.sh build alpine 3.20.2 --force
```

### Clean All Images
```bash
sudo scripts/build_rootfs.sh clean
```

This removes:
- All `.tar.gz` archives in `/var/sandbox/basefs/`
- The manifest file
- (Does NOT remove cached extractions in `/var/sandbox/cache/`)

### Clear Cache
```bash
sudo rm -rf /var/sandbox/cache/*
```

Forces re-extraction on next container launch.

## Adding New Distributions

Edit `scripts/build_rootfs.sh` and add to `DISTRO_CONFIGS`:

```bash
declare -A DISTRO_CONFIGS=(
    ["alpine:3.20.2"]="https://dl-cdn.alpinelinux.org/..."
    ["busybox:1.35.0"]="https://busybox.net/..."
    ["debian:12"]="https://your-debian-url..."  # Add new distro
)
```

Then implement `build_debian()` function following the pattern of existing builders.

## Troubleshooting

### Image not found
```
Error: Image 'alpine-3.20.2' not found at /var/sandbox/basefs/alpine-3.20.2.tar.gz
Hint: Run 'scripts/build_rootfs.sh build alpine 3.20.2' to build images
```

**Solution:** Build the image first.

### Extraction fails
```
Error: Failed to extract tarball /var/sandbox/basefs/alpine-3.20.2.tar.gz
```

**Solutions:**
1. Verify image integrity: `sudo scripts/build_rootfs.sh verify`
2. Rebuild if corrupted: `sudo scripts/build_rootfs.sh build alpine 3.20.2 --force`
3. Check disk space in `/var/sandbox/cache/`

### Permission denied
```
Error: Failed to create cache directory
```

**Solution:** Run with `sudo` as extraction requires root privileges.

## Best Practices

1. **Build images once**: Use the build script to create images, then reuse them
2. **Verify periodically**: Run `verify` to ensure images haven't been corrupted
3. **Cache management**: Monitor `/var/sandbox/cache/` size, clear unused images
4. **Version naming**: Use semantic versioning in image names (e.g., `alpine-3.20.2`)
5. **Minimize images**: Keep base images small (<50MB) for fast extraction

## Integration with Container Runtime

The C code automatically handles image lifecycle:

```c
// config.c automatically:
1. Validates tarball exists: /var/sandbox/basefs/<image>.tar.gz
2. Checks cache: /var/sandbox/cache/<image>/
3. Extracts if needed: tar -xzf
4. Uses cached path as overlay lowerdir
```

No manual extraction needed - just build once and run!
