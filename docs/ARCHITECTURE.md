# System Architecture

## High-Level Overview

```
┌─────────────────────────────────────────────────────────────┐
│                     User Interface                          │
│  ./feather_virt --image alpine-3.20.2 --name web1           │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│                  Configuration Layer                        │
│  • Parse CLI arguments                                      │
│  • Validate image exists (tarball)                          │
│  • Check/create cache extraction                            │
│  • Create config.json file                                  │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│                   Container Setup                           │
│  • Clone with namespaces (PID, NET, MNT, UTS, IPC, USER)    ;w│
│  • Setup UID/GID mapping                                    │
│  • Create overlay filesystem                                │
│  • Apply cgroup limits                                      │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│                Container Execution                          │
│  • chroot into merged overlay                               │
│  • Mount /proc, /dev                                        │
│  • Execute shell                                            │
└─────────────────────────────────────────────────────────────┘
```

## Image Lifecycle

```
Build Phase (scripts/build_rootfs.sh)
═════════════════════════════════════

  ┌──────────────┐
  │ Download     │  https://dl-cdn.alpinelinux.org/...
  │ Base Image   │
  └──────┬───────┘
         │
         ▼
  ┌──────────────┐
  │ Minimize     │  Remove docs, caches, etc.
  │ & Configure  │  Add /dev nodes, resolv.conf
  └──────┬───────┘
         │
         ▼
  ┌──────────────┐
  │ Compress     │  tar -czf alpine-3.20.2.tar.gz
  │ & Checksum   │  SHA256 → manifest.json
  └──────┬───────┘
         │
         ▼
  /var/sandbox/basefs/alpine-3.20.2.tar.gz


Runtime Phase (feather_virt)
════════════════════════════════

  ┌──────────────┐
  │ User runs    │  --image alpine-3.20.2
  │ container    │
  └──────┬───────┘
         │
         ▼
  ┌──────────────┐
  │ Check cache  │  /var/sandbox/cache/alpine-3.20.2/ ?
  └──────┬───────┘
         │
         ├── Not cached ──┐
         │                ▼
         │         ┌──────────────┐
         │         │ Extract      │  tar -xzf → cache/
         │         │ tarball      │
         │         └──────┬───────┘
         │                │
         └── Cached ──────┘
                          │
                          ▼
  ┌──────────────────────────────────────┐
  │ Use cached extraction as lowerdir    │
  │ /var/sandbox/cache/alpine-3.20.2/    │
  └──────────────────┬───────────────────┘
                     │
                     ▼
  ┌──────────────────────────────────────┐
  │ Create overlay per container         │
  │ lowerdir: /var/sandbox/cache/...     │
  │ upperdir: /var/sandbox/containers/   │
  │           web1-12345/upper/          │
  │ workdir:  /var/sandbox/containers/   │
  │           web1-12345/work/           │
  │ merged:   /var/sandbox/containers/   │
  │           web1-12345/rootfs/         │
  └──────────────────┬───────────────────┘
                     │
                     ▼
  ┌──────────────────────────────────────┐
  │ Container executes                   │
  │ chroot → merged overlay              │
  └──────────────────────────────────────┘
```

## Component Interaction

```
┌─────────────┐
│ main.c/zig  │  Entry point, CLI parsing, orchestration
└──────┬──────┘
       │
       ├─────────────────────────────────┐
       │                                 │
       ▼                                 ▼
┌─────────────┐                   ┌─────────────┐
│  config.c   │                   │ namespace.c │
│             │                   │             │
│ • Validate  │                   │ • UID/GID   │
│   image     │                   │   mapping   │
│ • Extract   │                   │ • /dev      │
│   tarball   │                   │   setup     │
│ • Cache     │                   └─────────────┘
│   mgmt      │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  overlay.c  │
│             │
│ • Create    │────────────────┐
│   overlay   │                │
│ • Cleanup   │                │
└─────────────┘                │
       │                       │
       ▼                       ▼
┌─────────────┐         ┌─────────────┐
│  cgroup.c   │         │ Container   │
│             │         │ Process     │
│ • Memory    │         │             │
│ • CPU       │         │ • chroot    │
│ • PID       │         │ • exec      │
│   limits    │         │   shell     │
└─────────────┘         └─────────────┘
```

## Filesystem Layer Architecture

```
Container View (inside container)
═════════════════════════════════

/                           ← Merged overlay
├── bin/                    ← From base image (read-only)
├── etc/                    ← From base image (read-only)
│   ├── hostname            ← Modified (copy-on-write)
│   └── resolv.conf         ← Modified (copy-on-write)
├── proc/                   ← Mounted inside container
├── dev/                    ← Created inside container
│   ├── null
│   └── zero
└── tmp/                    ← Writable (copy-on-write)


Host View (outside container)
════════════════════════════════

/var/sandbox/cache/alpine-3.20.2/   ← Read-only base (lowerdir)
├── bin/
├── etc/
└── ...

/var/sandbox/containers/web1-12345/
├── upper/                          ← Container changes (upperdir)
│   ├── etc/
│   │   └── hostname                ← Modified file
│   └── tmp/
│       └── user_created_file
├── work/                           ← Overlay metadata (workdir)
└── rootfs/                         ← Mounted overlay (merged)
    ├── bin/                        ← Appears to be from base
    ├── etc/
    │   ├── hostname                ← Actually from upper/
    │   └── resolv.conf             ← From base
    └── tmp/
        └── user_created_file       ← From upper/
```

## Namespace Isolation

```
┌────────────────────────────────────────────────────────┐
│                    Host System                         │
│  PID 1: systemd                                        │
│  PID 1234: feather_virt_dev (parent)                   │
└────────────┬───────────────────────────────────────────┘
             │
             │ clone(CLONE_NEWPID | CLONE_NEWNET | ...)
             │
             ▼
┌───────────────────────────────────────────────────────┐
│              Container Namespace                      │
│  ┌──────────────────────────────────────────────┐     │
│  │ PID Namespace                                │     │
│  │   PID 1: /bin/ash (init inside container)    │     │
│  │   PID 2: ps aux                              │     │
│  └──────────────────────────────────────────────┘     │
│  ┌──────────────────────────────────────────────┐     │
│  │ Mount Namespace                              │     │
│  │   / → overlay (merged view)                  │     │
│  │   /proc → proc                               │     │
│  └──────────────────────────────────────────────┘     │
│  ┌──────────────────────────────────────────────┐     │
│  │ Network Namespace                            │     │
│  │   (isolated, no interfaces)                  │     │
│  └──────────────────────────────────────────────┘     │
│  ┌──────────────────────────────────────────────┐     │
│  │ UTS Namespace                                │     │
│  │   hostname: web1                             │     │
│  └──────────────────────────────────────────────┘     │
│  ┌──────────────────────────────────────────────┐     │
│  │ User Namespace                               │     │
│  │   UID 0 (container) → UID 1000 (host)        │     │
│  └──────────────────────────────────────────────┘     │
└───────────────────────────────────────────────────────┘
```

## Cgroup Resource Hierarchy

```
/sys/fs/cgroup/
└── sandbox-web1/           ← Per-container cgroup
    ├── memory.max          → 128MB limit
    ├── cpu.max             → 50000/100000 (50% of 1 core)
    ├── pids.max            → 10 processes max
    └── cgroup.procs        → [12345] (container PID)
```

## Data Flow: Image to Container

```
1. Build Time
   ───────────
   Internet → download → minimize → compress
                                      ↓
                        /var/sandbox/basefs/alpine-3.20.2.tar.gz

2. First Launch
   ─────────────
   tarball → extract → /var/sandbox/cache/alpine-3.20.2/
                                ↓
                             (reused for all containers)

3. Container Creation
   ──────────────────
   cache/ (lowerdir)  ─┐
                        ├→ overlay → merged → chroot → exec
   containers/upper/   ─┘

4. Container Modifications
   ────────────────────────
   Write to /etc/hostname → Copy-on-Write
                            ↓
                    containers/web1-12345/upper/etc/hostname
                    (persists until container cleanup)

5. Container Cleanup
   ─────────────────
   umount merged → rm -rf containers/web1-12345/
                   (cache/ remains for reuse)
```

## Module Dependencies

```
main.c/zig
  ├── config.h
  │     ├── overlay.h
  │     └── (validates, extracts images)
  ├── namespace.h
  │     └── (UID/GID mapping, /dev setup)
  ├── overlay.h
  │     └── (filesystem layer management)
  └── cgroup.h
        └── (resource limits)

Build Dependencies:
  • libc (glibc/musl)
  • Linux kernel headers
  • tar, gzip (runtime)
  • json-c 
```

## Security Boundaries

```
┌──────────────────────────────────────────────────────┐
│              Security Layers                         │
├──────────────────────────────────────────────────────┤
│ 1. User Namespace     │ UID 0 (container) != root    │
│                       │ (mapped to host user)        │
├──────────────────────────────────────────────────────┤
│ 2. PID Namespace      │ Cannot see host processes    │
├──────────────────────────────────────────────────────┤
│ 3. Network Namespace  │ Isolated network stack       │
├──────────────────────────────────────────────────────┤
│ 4. Mount Namespace    │ Separate filesystem view     │
├──────────────────────────────────────────────────────┤
│ 5. Cgroups v2         │ Resource limits enforced     │
├──────────────────────────────────────────────────────┤
│ 6. Overlay FS         │ Read-only base image         │
│                       │ Copy-on-write for changes    │
└──────────────────────────────────────────────────────┘

Additional hardening needed for production:
  • seccomp (syscall filtering)
  • AppArmor/SELinux (MAC)
  • Capabilities dropping
```

## Performance Characteristics

| Operation | Time | Notes |
|-----------|------|-------|
| Image build | 30-60s | One-time per image |
| First container launch | 1-2s | Includes extraction |
| Cached container launch | <100ms | Uses extracted cache |
| Overlay mount | <10ms | Negligible overhead |
| Container cleanup | <50ms | umount + rm |

## Scalability Considerations

- **Shared cache**: N containers share 1 extracted base image
- **Disk usage**: `base_tarball + cache + (N * upper_changes)`
- **Memory**: 128MB per container (configurable in cgroup.c)
- **CPU**: 50% per container (configurable in cgroup.c)

Example with 10 containers:
```
alpine-3.20.2.tar.gz:     8 MB   (1x)
cache/alpine-3.20.2/:    15 MB   (1x shared)
10 × upper/:           ~100 KB   (per container)
Total:                  ~24 MB   (vs 150MB without sharing)
```
