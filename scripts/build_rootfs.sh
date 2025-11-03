#!/usr/bin/env bash
set -euo pipefail

# Configuration
BASE_DIR="/var/sandbox/basefs"
MANIFEST_FILE="$BASE_DIR/manifest.json"
TEMP_DIR="/tmp/rootfs-build"

# Supported distributions with their configurations
declare -A DISTRO_CONFIGS=(
    ["alpine:3.20.2"]="https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86_64/alpine-minirootfs-3.20.2-x86_64.tar.gz"
    ["busybox:1.35.0"]="https://busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox"
)

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[+]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[!]${NC} $1"
}

log_error() {
    echo -e "${RED}[✗]${NC} $1"
}

# Calculate SHA256 checksum
calculate_checksum() {
    local file="$1"
    sha256sum "$file" | awk '{print $1}'
}

# Verify image integrity
verify_image() {
    local image_path="$1"
    local expected_checksum="$2"
    
    if [ ! -f "$image_path" ]; then
        log_error "Image not found: $image_path"
        return 1
    fi
    
    local actual_checksum=$(calculate_checksum "$image_path")
    if [ "$actual_checksum" != "$expected_checksum" ]; then
        log_error "Checksum mismatch for $image_path"
        log_error "Expected: $expected_checksum"
        log_error "Got: $actual_checksum"
        return 1
    fi
    
    log_info "Image integrity verified: $image_path"
    return 0
}

# Build Alpine rootfs
build_alpine() {
    local version="$1"
    local target="$2"
    local url="${DISTRO_CONFIGS[alpine:$version]}"
    
    log_info "Building Alpine $version rootfs..."
    
    mkdir -p "$target"
    curl -fsSL "$url" -o "$TEMP_DIR/alpine-rootfs.tar.gz"
    
    tar -xzf "$TEMP_DIR/alpine-rootfs.tar.gz" -C "$target"
    
    # Minimize size
    rm -rf "$target"/{var/cache,usr/share/man,usr/share/doc,tmp}
    mkdir -p "$target"/{tmp,dev,proc,sys}
    
    # Create device nodes
    sudo mknod -m 666 "$target"/dev/null c 1 3 2>/dev/null || true
    sudo mknod -m 666 "$target"/dev/zero c 1 5 2>/dev/null || true
    sudo mknod -m 666 "$target"/dev/random c 1 8 2>/dev/null || true
    
    # Basic configuration
    echo "nameserver 1.1.1.1" > "$target/etc/resolv.conf"
    echo "sandbox" > "$target/etc/hostname"
    
    # Install minimal tools if apk is available
    if [ -f "$target/sbin/apk" ]; then
        log_info "Installing minimal package set..."
        sudo chroot "$target" /sbin/apk add --no-cache busybox 2>/dev/null || true
    fi
}

# Build Debian rootfs
build_debian() {
    local version="$1"
    local target="$2"
    local url="${DISTRO_CONFIGS[debian:$version]}"
    
    log_info "Building Debian $version rootfs..."
    
    mkdir -p "$target"
    curl -fsSL "$url" -o "$TEMP_DIR/debian-rootfs.tar.xz"
    
    tar -xJf "$TEMP_DIR/debian-rootfs.tar.xz" -C "$target"
    
    # Minimize size
    rm -rf "$target"/{var/cache,var/lib/apt,usr/share/man,usr/share/doc,tmp}
    mkdir -p "$target"/{tmp,dev,proc,sys}
    
    # Create device nodes
    sudo mknod -m 666 "$target"/dev/null c 1 3 2>/dev/null || true
    sudo mknod -m 666 "$target"/dev/zero c 1 5 2>/dev/null || true
    
    # Basic configuration
    echo "nameserver 1.1.1.1" > "$target/etc/resolv.conf"
    echo "sandbox" > "$target/etc/hostname"
}

# Build BusyBox rootfs
build_busybox() {
    local version="$1"
    local target="$2"
    local url="${DISTRO_CONFIGS[busybox:$version]}"
    
    log_info "Building BusyBox $version rootfs..."
    
    mkdir -p "$target"/{bin,sbin,etc,proc,sys,dev,usr/bin,usr/sbin,tmp}
    
    # Download busybox binary
    curl -fsSL "$url" -o "$target/bin/busybox"
    chmod +x "$target/bin/busybox"
    
    # Install busybox applets
    sudo chroot "$target" /bin/busybox --install -s
    
    # Create device nodes
    sudo mknod -m 666 "$target"/dev/null c 1 3 2>/dev/null || true
    sudo mknod -m 666 "$target"/dev/zero c 1 5 2>/dev/null || true
    
    # Minimal configuration
    cat > "$target/etc/passwd" <<EOF
root:x:0:0:root:/root:/bin/sh
nobody:x:65534:65534:nobody:/:/bin/false
EOF
    
    cat > "$target/etc/group" <<EOF
root:x:0:
nogroup:x:65534:
EOF
    
    echo "nameserver 1.1.1.1" > "$target/etc/resolv.conf"
    echo "sandbox" > "$target/etc/hostname"
}

# Compress and finalize image
finalize_image() {
    local target="$1"
    local archive="$2"
    
    log_info "Compressing rootfs..."
    tar -czf "$archive" -C "$(dirname "$target")" "$(basename "$target")"
    
    local size=$(du -sh "$archive" | awk '{print $1}')
    local checksum=$(calculate_checksum "$archive")
    
    log_info "Archive size: $size"
    log_info "SHA256: $checksum"
    
    echo "$checksum"
}

# Update manifest with image metadata
update_manifest() {
    local distro="$1"
    local version="$2"
    local archive="$3"
    local checksum="$4"
    
    local size=$(stat -f%z "$archive" 2>/dev/null || stat -c%s "$archive")
    local size_mb=$(echo "scale=2; $size / 1048576" | bc)
    local date=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
    
    # Initialize manifest if it doesn't exist
    if [ ! -f "$MANIFEST_FILE" ]; then
        echo '{"images":[]}' > "$MANIFEST_FILE"
    fi
    
    # Add entry to manifest using jq if available, otherwise use simple append
    if command -v jq &> /dev/null; then
        local temp_manifest=$(mktemp)
        jq --arg distro "$distro" \
           --arg version "$version" \
           --arg path "$archive" \
           --arg checksum "$checksum" \
           --arg size "$size" \
           --arg size_mb "$size_mb" \
           --arg date "$date" \
           '.images |= map(select(.distro != $distro or .version != $version)) + [{
               distro: $distro,
               version: $version,
               path: $path,
               checksum: $checksum,
               size_bytes: ($size | tonumber),
               size_mb: ($size_mb | tonumber),
               created: $date
           }]' "$MANIFEST_FILE" > "$temp_manifest"
        mv "$temp_manifest" "$MANIFEST_FILE"
    else
        log_warn "jq not found, manifest may not be properly formatted"
        # Fallback: simple JSON append (may create invalid JSON if multiple entries exist)
        cat >> "$MANIFEST_FILE" <<EOF
{
  "distro": "$distro",
  "version": "$version",
  "path": "$archive",
  "checksum": "$checksum",
  "size_bytes": $size,
  "size_mb": $size_mb,
  "created": "$date"
}
EOF
    fi
    
    log_info "Manifest updated: $MANIFEST_FILE"
}

# Main build function
build_rootfs() {
    local distro="$1"
    local version="$2"
    
    local target="$BASE_DIR/$distro-$version"
    local archive="$BASE_DIR/$distro-$version.tar.gz"
    
    # Check if already exists
    if [ -f "$archive" ]; then
        log_warn "$archive already exists. Use --force to rebuild."
        return 0
    fi
    
    # Create directories
    mkdir -p "$BASE_DIR" "$TEMP_DIR"
    
    # Build based on distro
    case "$distro" in
        alpine)
            build_alpine "$version" "$target"
            ;;
        debian)
            build_debian "$version" "$target"
            ;;
        busybox)
            build_busybox "$version" "$target"
            ;;
        *)
            log_error "Unknown distro: $distro"
            return 1
            ;;
    esac
    
    # Finalize and get checksum
    local checksum=$(finalize_image "$target" "$archive")
    
    # Update manifest
    update_manifest "$distro" "$version" "$archive" "$checksum"
    
    # Cleanup
    sudo rm -rf "$target" "$TEMP_DIR"
    
    log_info "Build complete: $archive"
}

# List available images
list_images() {
    log_info "Available base images:"
    echo ""
    
    if [ ! -f "$MANIFEST_FILE" ]; then
        log_warn "No manifest found. No images built yet."
        return
    fi
    
    if command -v jq &> /dev/null; then
        jq -r '.images[] | "\(.distro):\(.version) - \(.size_mb)MB - \(.created)"' "$MANIFEST_FILE"
    else
        cat "$MANIFEST_FILE"
    fi
}

# Verify all images in manifest
verify_all() {
    log_info "Verifying all images..."
    
    if [ ! -f "$MANIFEST_FILE" ]; then
        log_error "No manifest found"
        return 1
    fi
    
    local failed=0
    
    if command -v jq &> /dev/null; then
        while IFS=$'\t' read -r path checksum; do
            if ! verify_image "$path" "$checksum"; then
                ((failed++))
            fi
        done < <(jq -r '.images[] | "\(.path)\t\(.checksum)"' "$MANIFEST_FILE")
    else
        log_warn "jq not available, skipping verification"
        return 1
    fi
    
    if [ $failed -eq 0 ]; then
        log_info "All images verified successfully"
        return 0
    else
        log_error "$failed image(s) failed verification"
        return 1
    fi
}

# Usage information
usage() {
    cat <<EOF
Usage: $0 [OPTIONS] [COMMAND]

Build and manage minimal rootfs images for sandboxing.

COMMANDS:
    build <distro> <version>    Build a specific rootfs image
    build-all                   Build all supported distributions
    list                        List available images
    verify                      Verify integrity of all images
    clean                       Remove all images and manifest

SUPPORTED DISTRIBUTIONS:
    alpine:3.20.2              Alpine Linux (target: <10MB)
    debian:12                  Debian Bookworm slim
    busybox:1.36.1             Minimal BusyBox-based system

OPTIONS:
    -h, --help                 Show this help message
    --force                    Force rebuild even if image exists

EXAMPLES:
    $0 build alpine 3.20.2
    $0 build-all
    $0 list
    $0 verify

STORAGE:
    Images: $BASE_DIR/<distro>-<version>.tar.gz
    Manifest: $MANIFEST_FILE
EOF
}

# Main script logic
main() {
    local force=0
    
    # Parse options
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                usage
                exit 0
                ;;
            --force)
                force=1
                shift
                ;;
            build)
                if [ $# -lt 3 ]; then
                    log_error "Usage: $0 build <distro> <version>"
                    exit 1
                fi
                if [ $force -eq 1 ] && [ -f "$BASE_DIR/$2-$3.tar.gz" ]; then
                    rm -f "$BASE_DIR/$2-$3.tar.gz"
                fi
                build_rootfs "$2" "$3"
                exit 0
                ;;
            build-all)
                log_info "Building all supported distributions..."
                build_rootfs alpine 3.20.2
                build_rootfs busybox 1.35.0
                log_info "All builds complete"
                exit 0
                ;;
            list)
                list_images
                exit 0
                ;;
            verify)
                verify_all
                exit $?
                ;;
            clean)
                log_warn "Removing all images and manifest..."
                rm -rf "$BASE_DIR"/*.tar.gz "$MANIFEST_FILE"
                log_info "Cleanup complete"
                exit 0
                ;;
            *)
                log_error "Unknown command: $1"
                usage
                exit 1
                ;;
        esac
    done
    
    # Default: show usage
    usage
}

# Ensure running as root for some operations
if [ $EUID -ne 0 ] && [ "${1:-}" = "build" ]; then
    log_warn "Some operations require root privileges. Consider running with sudo."
fi

main "$@"
