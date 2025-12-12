# Quick Reference

## Essential Commands

### Building Images
```bash
# Build all supported images
sudo scripts/build_rootfs.sh build-all

# Build specific image
sudo scripts/build_rootfs.sh build alpine 3.20.2
sudo scripts/build_rootfs.sh build busybox 1.35.0

# Force rebuild
sudo scripts/build_rootfs.sh build alpine 3.20.2 --force
```

### Managing Images
```bash
# List available images
sudo ./zig-out/bin/feather_virt --list-images
sudo scripts/build_rootfs.sh list

# Verify image integrity
sudo scripts/build_rootfs.sh verify

# Clean all images
sudo scripts/build_rootfs.sh clean
```

### Running Containers
```bash
# Basic container
sudo ./zig-out/bin/feather_virt --image alpine-3.20.2

# Named container with custom shell
sudo ./zig-out/bin/feather_virt --image alpine-3.20.2 --shell /bin/ash --name web1

# BusyBox container
sudo ./zig-out/bin/feather_virt --image busybox-1.35.0 --name minimal
```

## Directory Structure

```
/var/sandbox/
├── basefs/              # Compressed image tarballs
│   ├── alpine-3.20.2.tar.gz
│   ├── busybox-1.35.0.tar.gz
│   └── manifest.json
├── cache/               # Extracted images (auto-managed)
│   ├── alpine-3.20.2/
│   └── busybox-1.35.0/
└── containers/          # Per-container overlays
    ├── web1-12345/
    │   ├── upper/
    │   ├── work/
    │   └── rootfs/
    └── web2-12346/
        ├── upper/
        ├── work/
        └── rootfs/
```

## Image Names vs. Paths

| User specifies | Tarball location | Cached extraction |
|----------------|------------------|-------------------|
| `alpine-3.20.2` | `/var/sandbox/basefs/alpine-3.20.2.tar.gz` | `/var/sandbox/cache/alpine-3.20.2/` |
| `busybox-1.35.0` | `/var/sandbox/basefs/busybox-1.35.0.tar.gz` | `/var/sandbox/cache/busybox-1.35.0/` |

## Common Workflows

### First-time Setup
```bash
zig build
sudo zig build setup-dirs
sudo ./scripts/build_rootfs.sh build alpine 3.20.2
sudo ./zig-out/bin/feather_virt --image alpine-3.20.2
```

### Clean and Rebuild
```bash
# Remove compressed images
sudo ./scripts/build_rootfs.sh clean

# Remove cache (forces re-extraction)
sudo rm -rf /var/sandbox/cache/*


## Troubleshooting

| Error | Solution |
|-------|----------|
| Image not found | `sudo scripts/build_rootfs.sh build <distro> <version>` |
| Permission denied | Run with `sudo` |
| Extraction fails | Check disk space, verify image, rebuild with `--force` |
| Container won't start | Check kernel version (4.18+), cgroup v2 support |

## Performance Tips

- **First launch**: Slow (extracts tarball)
- **Subsequent launches**: Fast (uses cache)
- **Multiple containers**: Share cached base image
- **Clear cache**: Only if needed to free disk space

## Resource Limits (per container)

- Memory: 128 MB
- CPU: 50% of one core
- PIDs: Max 10 processes
- Network: Isolated (no network by default)

## File Locations

| Component | Path |
|-----------|------|
| Binary | `./zig-out/bin/feather_virt` |
| Build script | `scripts/build_rootfs.sh` |
| Images | `/var/sandbox/basefs/*.tar.gz` |
| Cache | `/var/sandbox/cache/` |
| Containers | `/var/sandbox/containers/` |
| Cgroups | `/sys/fs/cgroup/sandbox-*` |
| Manifest | `/var/sandbox/basefs/manifest.json` |

```
