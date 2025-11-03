# Feather Virt Dev - Lightweight Container Runtime

A modular, lightweight container runtime using Linux namespaces, cgroups v2, and overlay filesystems.

## Features

- **Namespace Isolation**: PID, network, mount, UTS, IPC, and user namespaces
- **Resource Limits**: Memory (128MB), CPU (50%), and process count (10) via cgroups v2
- **Overlay Filesystem**: Copy-on-write root filesystem with per-container persistence
- **Multiple Base Images**: Support for different rootfs images
- **Named Containers**: Human-readable container identification
- **Configurable Shell**: Choose your preferred shell

## Documentation

- **[README.md](README.md)** - This file (overview and usage)
- **[BUILD_GUIDE.md](docs/BUILD_GUIDE.md)** - Detailed image building documentation
- **[QUICKREF.md](docs/QUICKREF.md)** - Quick reference for common commands
- **[ARCHITECTURE.md](docs/ARCHITECTURE.md)** - System architecture and internals

## Project Structure

```
.
├── main.c          # Main application and CLI argument parsing
├── config.c/h      # Configuration management and image validation
├── overlay.c/h     # Overlay filesystem setup and cleanup
├── cgroup.c/h      # Cgroup v2 resource limit management
├── namespace.c/h   # Namespace and UID/GID mapping
├── Makefile        # Build configuration
└── README.md       # This file
```

## Building

```bash
make
```

## Setup

### 1. Create required directories:

```bash
sudo make setup-dirs
```

### 2. Build base images:

The project includes a script to build and manage rootfs images:

```bash
# Build all supported images
sudo scripts/build_rootfs.sh build-all

# Or build specific images
sudo scripts/build_rootfs.sh build alpine 3.20.2
sudo scripts/build_rootfs.sh build busybox 1.35.0

# List available images
sudo scripts/build_rootfs.sh list

# Verify image integrity
sudo scripts/build_rootfs.sh verify
```

Images are stored as compressed tarballs in `/var/sandbox/basefs/` and automatically extracted to `/var/sandbox/cache/` on first use.

**Supported images:**
- `alpine-3.20.2` - Alpine Linux (~8-10MB compressed)
- `busybox-1.35.0` - Minimal BusyBox system (~2-3MB compressed)

## Usage

### List Available Images

```bash
sudo ./feather_virt_dev --list-images
```

### Run a Container

```bash
# Basic usage with Alpine
sudo ./feather_virt_dev --image alpine-3.20.2

# With custom shell and name
sudo ./feather_virt_dev --image alpine-3.20.2 --shell /bin/ash --name test1

# With all options
sudo ./feather_virt_dev --image debian-12 --shell /bin/bash --name webserver
```

### Command-Line Options

- `--image <name>` - Select base rootfs image (required)
- `--list-images` - Show available base images and exit
- `--shell <path>` - Shell to execute (default: `/bin/sh`)
- `--name <container-name>` - Human-readable container name (default: `unnamed`)
- `-h, --help` - Show help message

## Image Validation

The tool validates that:
1. The specified image tarball (`.tar.gz`) exists in `/var/sandbox/basefs/`
2. The image is a valid compressed archive
3. The image is automatically extracted to `/var/sandbox/cache/` on first use
4. Cached images are reused for subsequent container launches

The build script (`scripts/build_rootfs.sh`) maintains a manifest with checksums for integrity verification.

## Resource Limits

Each container is limited to:
- **Memory**: 128 MB
- **CPU**: 50% of one core
- **Processes**: Maximum 10 PIDs

Limits are enforced via cgroup v2 at `/sys/fs/cgroup/sandbox-<name>`.

## Container Isolation

Each container gets:
- Private PID namespace (init is PID 1 inside)
- Private network namespace (isolated networking)
- Private mount namespace (separate filesystem view)
- Private UTS namespace (custom hostname)
- Private IPC namespace (isolated IPC)
- User namespace with UID/GID mapping

## Filesystem Layout

```
/var/sandbox/
├── basefs/
│   ├── alpine-3.20.2.tar.gz    # Compressed base image
│   ├── busybox-1.35.0.tar.gz   # Compressed base image
│   └── manifest.json           # Image metadata with checksums
├── cache/
│   ├── alpine-3.20.2/          # Extracted image (read-only lower layer)
│   └── busybox-1.35.0/         # Extracted image (read-only lower layer)
└── containers/
    └── test1-12345/            # Per-container directory
        ├── upper/              # Container-specific changes (copy-on-write)
        ├── work/               # Overlay work directory
        └── rootfs/             # Merged overlay mount point
```

**Image lifecycle:**
1. Images are built as `.tar.gz` archives in `/var/sandbox/basefs/`
2. On first use, images are extracted to `/var/sandbox/cache/<image-name>/`
3. Extracted images are reused for all containers using that base image
4. Each container gets its own overlay with copy-on-write in `upper/`

## Requirements

- Linux kernel 4.18+ (for cgroup v2)
- Root privileges (for namespace and mount operations)
- `tar` and `gzip` utilities
- Internet connection (for building images)

## Quick Start

```bash
# 1. Compile
make

# 2. Setup directories
sudo make setup-dirs

# 3. Build images
sudo make build-images
# OR manually:
# sudo scripts/build_rootfs.sh build-all

# 4. Run a container
sudo ./feather_virt_dev --image alpine-3.20.2 --name mycontainer
```

## Example Session

```bash
# Build base images
$ sudo scripts/build_rootfs.sh build-all
[+] Building Alpine 3.20.2 rootfs...
[+] Compressing rootfs...
[+] Archive size: 8.2M
[+] Build complete: /var/sandbox/basefs/alpine-3.20.2.tar.gz

# List available images
$ sudo ./feather_virt_dev --list-images
Available base images in /var/sandbox/basefs:
----------------------------------------
  - alpine-3.20.2                (8.23 MB)
  - busybox-1.35.0               (2.45 MB)

# Start Alpine container (first time - will extract)
$ sudo ./feather_virt_dev --image alpine-3.20.2 --shell /bin/ash --name web1
Extracting image 'alpine-3.20.2' to cache...
Image extracted successfully
[host] Configuration:
  Image:     /var/sandbox/cache/alpine-3.20.2
  Shell:     /bin/ash
  Name:      web1

[host] Spawning isolated container...
[host] overlay root mounted at /var/sandbox/containers/web1-12345/rootfs
[sandbox child] pid=1 container='web1' merged root: /var/sandbox/containers/web1-12345/rootfs
/ # hostname
web1
/ # ps aux
PID   USER     TIME  COMMAND
    1 root      0:00 /bin/ash
    2 root      0:00 ps aux
/ # exit
[host] cleaning up overlay at /var/sandbox/containers/web1-12345/rootfs
[host] container 'web1' exited code=0

# Subsequent launches use cached image (no extraction)
$ sudo ./feather_virt_dev --image alpine-3.20.2 --name web2
[host] Configuration:
  Image:     /var/sandbox/cache/alpine-3.20.2
  Shell:     /bin/sh
  Name:      web2
...
```

## Cleanup

The runtime automatically:
1. Unmounts the overlay filesystem
2. Removes per-container directories
3. Cleans up cgroup entries

## Goals
1. Multi-Architecture Support

    - Cross-compilation tooling: Ability to compile rootfs and binaries for x86_64, arm64, riscv64, and specialized devices (Jetson, Coral NPU, etc.).
    
    - Architecture-specific optimizations: CPU/GPU/TPU instruction sets, SIMD support (NEON, AVX2/AVX512, RISC-V vector extensions).
    
    - Configurable target profiles: Let the executable or build system select the target architecture, toolchain, and dependencies automatically.

2. Lightweight and Modular RootFS

    - Minimal base images: Only essential libraries and binaries; avoid bloated images.
    
    - Modular package inclusion: Optionally include inference frameworks, drivers, and edge libraries.
    
    - Update and patch mechanism: Easy way to add security updates or framework upgrades without rebuilding entire images.

3. Hardware Abstraction & Drivers

    - GPU/TPU/NPU support: Include CUDA, TensorRT, OpenCL, or Edge TPU runtime libraries as optional modules.
    
    - Flexible device detection: Runtime detection of available accelerators and dynamic selection of inference backend.
    
    - Plug-and-play device drivers: Precompiled or easily cross-compiled drivers for different edge boards.

4. Edge Inference Optimization

    - Framework integration: Support for ONNX Runtime, TensorFlow Lite, PyTorch Mobile, or OpenVINO.
    
    - Model deployment tooling: Allow easy injection of pre-trained models into images.
    
    - Quantization & pruning hooks: Provide options to build images with optimized models for edge inference (int8, fp16, sparsity).

5. Build & Deployment Automation

    - Config-driven builds: Already planning TOML configs, extend to target device profiles and model deployments.
    
    - CI/CD integration: Auto-build images for multiple architectures when code/models change.
    
    - Versioning and reproducibility: Tag images by architecture, framework version, and model version.

6. Monitoring & Diagnostics

    - Edge telemetry: Include optional lightweight monitoring tools to track inference latency, memory usage, CPU/GPU load.
    
    - Logging & debugging hooks: Allow devs to inspect runtime behavior without intrusive modifications.

7. Extensibility & Ecosystem

    - Plugin system: Let devs add support for new accelerators or inference frameworks without changing core code.
    
    - Community-friendly structure: Make configs, image manifests, and build scripts easily readable and extensible.
    
    - Documentation templates: Encourage developers to describe how to target new hardware architectures.

## Security Notes

- This is a development/educational tool
- Requires root privileges
- User namespace provides some isolation, but full security hardening is needed for production
- Consider adding seccomp filters, AppArmor/SELinux profiles for production use

## License

Apache License 2.0 - See [LICENSE.txt](LICENSE.txt)
