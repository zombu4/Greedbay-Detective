#!/bin/sh
# Checks a release tag before anything is built (PLAN 10):
#   - the tag is v<version> (a final release) or v<version>-rc<N> (a
#     pre-release that tests the pipeline), <version> being the one
#     project(VERSION) sets in CMakeLists.txt, the only place it is defined;
#   - CHANGELOG.md has a section "## <version>";
#   - a final release has the LICENSE file (owner decision: no LICENSE until
#     the lawyer-reviewed one exists, so no final release before it);
#   - the tagged commit is on main.
#
# usage: release-check.sh <tag>   (run from the repository, full history)
set -eu

tag=$1

fail() {
    echo "RELEASE CHECK FAILED: $*"
    exit 1
}

version=$(sed -n 's/^    VERSION \([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)$/\1/p' CMakeLists.txt)
[ -n "$version" ] && [ "$(printf '%s\n' "$version" | wc -l)" -eq 1 ] || fail "no single VERSION line in CMakeLists.txt"

if [ "$tag" = "v$version" ]; then
    final=yes
elif printf '%s\n' "$tag" | grep -Eqx "v$(printf '%s' "$version" | sed 's/\./\\./g')-rc[0-9]+"; then
    final=no
else
    fail "tag $tag is neither v$version nor v$version-rcN"
fi

grep -Eq "^## $(printf '%s' "$version" | sed 's/\./\\./g')( |$)" CHANGELOG.md || fail "CHANGELOG.md has no section \"## $version\""

if [ "$final" = yes ] && [ ! -f LICENSE ]; then
    fail "a final release needs the LICENSE file (none until the lawyer-reviewed one exists)"
fi

git merge-base --is-ancestor HEAD origin/main || fail "the tagged commit is not on main"

echo "release check passed: $tag (version $version, final: $final)"
