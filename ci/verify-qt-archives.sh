#!/bin/sh
# Checks the Qt archives that aqtinstall kept (--keep --archive-dest) against
# the committed checksum list. Fails when an archive on the list is missing,
# has a different SHA-256, or when the folder holds an archive not on the list.
#
# usage: verify-qt-archives.sh <list file> <mac|windows|linux> <archive folder>
#
# List lines are "<sha256>  <os>/<archive name as published on download.qt.io>".
# aqtinstall saves each archive without the "<version>-<n>-<build id>" prefix of
# the published name, so the prefix is removed before matching.
set -eu

list=$1
os=$2
dir=$3

# Hashed from stdin so the output never contains the file name: GNU sha256sum
# escapes names with backslashes (Windows paths) by prefixing the line with "\".
hash_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum < "$1"
    else
        shasum -a 256 < "$1"
    fi | cut -d' ' -f1
}

expected=$(mktemp)
trap 'rm -f "$expected"' EXIT

grep "  $os/" "$list" | while read -r hash path; do
    name=${path#"$os"/}
    kept=$(printf '%s' "$name" | sed -E 's/^[0-9]+\.[0-9]+\.[0-9]+-[0-9]+-[0-9]{12}//')
    printf '%s %s\n' "$hash" "$kept"
done > "$expected"

if [ ! -s "$expected" ]; then
    echo "no entries for $os in $list"
    exit 1
fi

status=0
while read -r hash kept; do
    file="$dir/$kept"
    if [ ! -f "$file" ]; then
        echo "missing: $kept"
        status=1
        continue
    fi
    if [ "$(hash_file "$file")" = "$hash" ]; then
        echo "ok: $kept"
    else
        echo "SHA-256 MISMATCH: $kept"
        status=1
    fi
done < "$expected"

for file in "$dir"/*; do
    [ -e "$file" ] || continue
    name=$(basename "$file")
    if ! cut -d' ' -f2- "$expected" | grep -Fxq "$name"; then
        echo "not on the list: $name"
        status=1
    fi
done

exit $status
