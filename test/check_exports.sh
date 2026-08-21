#!/usr/bin/env sh
set -eu

library=$(find zig-out/lib -type f -name 'libdvdcss.so.*' | sort | tail -n 1)
if [ -z "$library" ]; then
    echo "shared libdvdcss artifact not found" >&2
    exit 1
fi

actual=$(mktemp)
trap 'rm -f "$actual"' EXIT

nm -D --defined-only "$library" \
    | awk '{print $3}' \
    | grep -E '^(dvdcss|dvdcpxm)_' \
    | LC_ALL=C sort -u > "$actual"

diff -u test/expected_exports.txt "$actual"
