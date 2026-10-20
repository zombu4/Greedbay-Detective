#!/bin/sh
# Prints the CHANGELOG.md section of a release tag's version (the lines
# after "## <version>" up to the next "## " heading): the release notes.
#
# usage: changelog-section.sh <tag>   (v1.2.3 or v1.2.3-rc1 -> 1.2.3)
set -eu

version=${1#v}
version=${version%%-rc*}

awk -v version="$version" '
    /^## / {
        if (found) exit
        if ($2 == version) { found = 1; next }
    }
    found { print }
    END { if (!found) exit 1 }
' CHANGELOG.md
