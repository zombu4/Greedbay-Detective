#!/bin/sh
# Install test, Ubuntu (PLAN 11 "Install / uninstall"): the .deb installed
# with apt (its dependencies from the Ubuntu archive) -> the app runs on a
# virtual display and makes its work folder -> the user's data removed as the
# in-app uninstall removes it (the app's own --uninstall-data) -> the package
# removed. Afterwards only the listed paths are checked; nothing is searched
# for.
#
# usage: linux.sh <the .deb> <app identifier> <package name> <install prefix>
set -eu

deb=$1
identifier=$2
package=$3
prefix=$4
work="$HOME/.local/share/$identifier"
program="$prefix/bin/$identifier"
desktop="/usr/share/applications/$package.desktop"

fail() {
    echo "INSTALL TEST FAILED: $*"
    exit 1
}

[ ! -e "$work" ] || fail "a work folder exists before the test: $work"

sudo apt-get install -y "$(realpath "$deb")"
[ -x "$program" ] || fail "the program is not at $program"
[ -f "$desktop" ] || fail "the desktop entry is not at $desktop"

# Run it until it has made its work folder (at most 90 s), then stop it.
xvfb-run -a "$program" &
waited=0
while [ ! -f "$work/.workfolder" ]; do
    [ "$waited" -lt 90 ] || fail "the app made no work folder in 90 s"
    sleep 1
    waited=$((waited + 1))
done
sleep 3
# By its full command line: Linux keeps only 15 characters of a process name.
pkill -TERM -f "^$program" || true
wait || true
[ -f "$work/$identifier.sqlite" ] || fail "the app made no database"

# Keep, then delete.
"$program" --uninstall-data=keep
[ -f "$work/$identifier.sqlite" ] || fail "keep removed the database"
[ ! -e "$work/logs" ] || fail "keep left the logs"
[ ! -e "$work/browser" ] || fail "keep left the browser profiles"
"$program" --uninstall-data=delete
[ ! -e "$work" ] || fail "the work folder is still there: $work"

sudo dpkg --remove "$package"
[ ! -e "$program" ] || fail "the program is still there"
[ ! -e "$desktop" ] || fail "the desktop entry is still there"
echo "install test passed"
