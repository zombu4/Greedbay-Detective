#!/bin/sh
# Checks a downloaded build tool against its pinned SHA-256 in ci/tools.sha256.
#
# usage: check-tool.sh <tools.sha256> <downloaded file>
set -eu

list=$1
file=$2
name=$(basename "$file")

expected=$(awk -v n="$name" '$2 == n { print $1 }' "$list")
if [ -z "$expected" ]; then
    echo "no pinned checksum for $name"
    exit 1
fi

# The file is hashed from stdin so the output never contains its name: GNU
# sha256sum escapes names with backslashes (Windows paths) by prefixing the
# whole line with "\", which would break the comparison.
if command -v sha256sum >/dev/null 2>&1; then
    actual=$(sha256sum < "$file" | cut -d' ' -f1)
else
    actual=$(shasum -a 256 < "$file" | cut -d' ' -f1)
fi

if [ "$actual" != "$expected" ]; then
    echo "SHA-256 MISMATCH: $name"
    exit 1
fi
echo "ok: $name"
