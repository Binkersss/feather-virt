# RootFS Builder Documentation

## Overview

The `build_rootfs.sh` script creates and manages minimal rootfs images for multiple Linux distributions, designed for secure sandboxing environments.

## Supported Distributions

| Distribution | Version | Target Size | Use Case |
|-------------|---------|-------------|----------|
| Alpine Linux | 3.20.2 | < 10MB | Production, minimal footprint |
| Debian | 12 (Bookworm) | ~30MB | Compatibility, full toolchain |
| BusyBox | 1.36.1 | < 5MB | Ultra-minimal, embedded |

## Quick Start

```bash
# Build a specific distribution
sudo ./build_rootfs.sh build alpine 3.20.2

# Build all supported distributions
sudo ./build_rootfs.sh build-all

# List available images
./build_rootfs.sh list

# Verify image integrity
./build_rootfs.sh verify
```

## Directory Structure

```
/var/sandbox/basefs/
├── alpine-3.20.2.tar.gz      # Compressed rootfs image
├── debian-12.tar.gz
├── busybox-1.36.1.tar.gz
└── manifest.json              # Metadata for all images
```

## Manifest Format

The manifest file (`manifest.json`) tracks all built images:

```json
{
  "images": [
    {
      "distro": "alpine",
      "version": "3.20.2",
      "path": "/var/sandbox/basefs/alpine-3.20.2.tar.gz",
      "checksum": "sha256:abc123...",
      "size_bytes": 8388608,
      "size_mb": 8.0,
      "created": "2025-11-02T12:00:00Z"
    }
  ]
}
```

## Size Optimization Targets

### Alpine Linux (< 10MB)
- **Base size**: ~8-9MB compressed
- **Optimizations applied**:
  - Removed `/var/cache`, `/usr/share/man`, `/usr/share/doc`
  - Minimal device nodes only
  - No package cache
  - Static resolv.conf

### BusyBox (< 5MB)
- **Base size**: ~2-4MB compressed
- **Optimizations applied**:
  - Single static binary with symlinks
  - Minimal `/etc` configuration
  - No package manager
  - Essential directories only

### Debian (< 40MB)
- **Base size**: ~25-35MB compressed
- **Optimizations applied**:
  - Slim base image
  - APT cache removed
  - Documentation removed
  - Essential packages only

## Verification Process

The script implements SHA256 checksum verification:

1. **Build Time**: Calculate checksum after compression
2. **Verification**: Compare stored checksum against actual file
3. **Manifest**: Store checksum in manifest for auditing

```bash
# Verify all images
./build_rootfs.sh verify

# Output:
# [+] Image integrity verified: /var/sandbox/basefs/alpine-3.20.2.tar.gz
# [+] Image integrity verified: /var/sandbox/basefs/debian-12.tar.gz
# [+] All images verified successfully
```

## Usage Examples

### Building a Single Distribution

```bash
sudo ./build_rootfs.sh build alpine 3.20.2
```

**Output**:
```
[+] Building Alpine 3.20.2 rootfs...
[+] Compressing rootfs...
[+] Archive size: 8.5M
[+] SHA256: a1b2c3d4...
[+] Manifest updated: /var/sandbox/basefs/manifest.json
[+] Build complete: /var/sandbox/basefs/alpine-3.20.2.tar.gz
```

### Rebuilding an Existing Image

```bash
# Will skip if exists
sudo ./build_rootfs.sh build alpine 3.20.2

# Force rebuild
sudo ./build_rootfs.sh --force build alpine 3.20.2
```

### Listing Available Images

```bash
./build_rootfs.sh list
```

**Output**:
```
[+] Available base images:

alpine:3.20.2 - 8.5MB - 2025-11-02T12:00:00Z
debian:12 - 28.3MB - 2025-11-02T12:05:00Z
busybox:1.36.1 - 3.2MB - 2025-11-02T12:10:00Z
```

## Integration with Sandbox Runtime

### Extracting an Image

```bash
# Extract for use
sudo mkdir -p /var/sandbox/containers/instance-001
sudo tar -xzf /var/sandbox/basefs/alpine-3.20.2.tar.gz \
    -C /var/sandbox/containers/instance-001
```

### Verification Before Use

```python
import json
import hashlib

def verify_image(manifest_path, distro, version):
    with open(manifest_path) as f:
        manifest = json.load(f)
    
    for img in manifest['images']:
        if img['distro'] == distro and img['version'] == version:
            with open(img['path'], 'rb') as f:
                actual = hashlib.sha256(f.read()).hexdigest()
            
            if actual == img['checksum']:
                return True, img
            else:
                raise ValueError(f"Checksum mismatch for {distro}:{version}")
    
    raise ValueError(f"Image not found: {distro}:{version}")

# Usage
ok, image = verify_image('/var/sandbox/basefs/manifest.json', 'alpine', '3.20.2')
print(f"Verified: {image['path']} ({image['size_mb']}MB)")
```

## Minimum Viable RootFS Components

### Required Directories
- `/bin`, `/sbin` - Essential binaries
- `/etc` - Configuration files
- `/dev` - Device nodes (null, zero, random)
- `/proc`, `/sys` - Virtual filesystems (mount points)
- `/tmp` - Temporary files

### Essential Device Nodes
```bash
mknod -m 666 /dev/null c 1 3    # Null device
mknod -m 666 /dev/zero c 1 5    # Zero device
mknod -m 666 /dev/random c 1 8  # Random entropy
```

### Minimal Configuration Files
- `/etc/resolv.conf` - DNS resolution
- `/etc/hostname` - System hostname
- `/etc/passwd` - User database (BusyBox)
- `/etc/group` - Group database (BusyBox)

## Security Considerations

1. **Image Integrity**: Always verify checksums before deployment
2. **Read-Only**: Mount rootfs as read-only in production
3. **Overlay FS**: Use overlayfs for writable layers
4. **Regular Updates**: Rebuild images monthly for security patches
5. **Minimal Attack Surface**: Keep images as small as possible

## Troubleshooting

### Build Fails: Permission Denied
```bash
# Ensure running as root for mknod operations
sudo ./build_rootfs.sh build alpine 3.20.2
```

### Checksum Mismatch
```bash
# Rebuild the image
sudo ./build_rootfs.sh --force build alpine 3.20.2

# Verify
./build_rootfs.sh verify
```

### Download Errors
```bash
# Check network connectivity
curl -I https://dl-cdn.alpinelinux.org/

# Clear temp files and retry
rm -rf /tmp/rootfs-build
sudo ./build_rootfs.sh build alpine 3.20.2
```

## Performance Benchmarks

| Distribution | Build Time | Compressed Size | Extraction Time |
|-------------|-----------|----------------|----------------|
| Alpine 3.20.2 | ~30s | 8.5MB | ~2s |
| Debian 12 | ~60s | 28MB | ~5s |
| BusyBox 1.36.1 | ~10s | 3.2MB | ~1s |

*Benchmarked on: Intel i7, 16GB RAM, SSD*

## Future Enhancements

- [ ] Add Alpine 3.19 LTS support
- [ ] Implement incremental builds
- [ ] Add compression level options (gzip vs xz vs zstd)
- [ ] Support for ARM64 architecture
- [ ] Automated testing of built images
- [ ] Image signing with GPG

## References

- [Alpine Linux Mini Root Filesystem](https://alpinelinux.org/downloads/)
- [Debian Docker Base Images](https://github.com/debuerreotype/docker-debian-artifacts)
- [BusyBox Binary Downloads](https://busybox.net/downloads/binaries/)

## License

MIT License - See LICENSE file for details
