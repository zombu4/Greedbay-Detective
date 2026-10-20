#!/bin/sh
# Install test, macOS (PLAN 11 "Install / uninstall"): the .dmg -> the app
# copied out as a user drags it -> the app runs and makes its work folder ->
# the user's data removed as the in-app uninstall removes it (the app's own
# --uninstall-data) -> the copy removed. Afterwards only the listed paths are
# checked; nothing is searched for.
#
# usage: macos.sh <the .dmg> <app identifier>
set -eu

dmg=$1
identifier=$2
mount="$RUNNER_TEMP/dmg"
apps="$RUNNER_TEMP/Applications"
work="$HOME/Library/Application Support/$identifier"
cache="$HOME/Library/Caches/$identifier"

fail() {
    echo "INSTALL TEST FAILED: $*"
    exit 1
}

[ ! -e "$work" ] || fail "a work folder exists before the test: $work"

mkdir -p "$mount" "$apps"
hdiutil attach "$dmg" -nobrowse -readonly -mountpoint "$mount"
[ -L "$mount/Applications" ] || fail "the .dmg has no link to /Applications"
cp -R "$mount/$identifier.app" "$apps/"
hdiutil detach "$mount"
app="$apps/$identifier.app"
[ -x "$app/Contents/MacOS/$identifier" ] || fail "the app is not in the .dmg"

# Run it until it has made its work folder (at most 90 s), then stop it.
"$app/Contents/MacOS/$identifier" &
pid=$!
waited=0
while [ ! -f "$work/.workfolder" ]; do
    [ "$waited" -lt 90 ] || fail "the app made no work folder in 90 s"
    kill -0 "$pid" 2>/dev/null || fail "the app ended before it made its work folder"
    sleep 1
    waited=$((waited + 1))
done
sleep 3
kill "$pid"
wait "$pid" || true
[ -f "$work/$identifier.sqlite" ] || fail "the app made no database"

# Keep, then delete.
"$app/Contents/MacOS/$identifier" --uninstall-data=keep
[ -f "$work/$identifier.sqlite" ] || fail "keep removed the database"
[ -f "$work/.workfolder" ] || fail "keep removed the marker"
[ ! -e "$work/logs" ] || fail "keep left the logs"
[ ! -e "$work/browser" ] || fail "keep left the browser profiles"
"$app/Contents/MacOS/$identifier" --uninstall-data=delete
[ ! -e "$work" ] || fail "the work folder is still there: $work"
[ ! -e "$cache" ] || fail "the cache folder is still there: $cache"

rm -rf "$app"
[ ! -e "$app" ] || fail "the app is still there"
echo "install test passed"
