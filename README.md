# Greedbay Detective
**Fight greedy garbage sellers with knowledge.**

## Why this exists

A lot of used electronics on eBay was pulled out of the trash by recyclers and listed at
collector prices: test gear, vintage audio, old computers. Some of it is "untested", some of it
plainly broken, and the shipping is often padded to $40 for something that fits in a shoebox.

eBay doesn't make it easy to see what something is really worth. The sold-items page shows the
asking price even when a much lower Best Offer was accepted, so the "sold" prices you see are
often inflated. Checking comps by hand means scrolling through hundreds of listings, filtering
out the junk, and doing the math yourself.

Greedbay Detective does that work for you. Paste a listing link and it shows:
- what the same item **actually** sold for, including the real accepted Best Offer price where
  eBay has it
- how many others are for sale right now, and for how much
- whether the shipping charge is normal or a rip-off
- a fair value estimate, so you know if the seller is reaching

Every lookup is saved, so you can come back later and see how prices move over time.

## Status
Under active development. No release yet.

[![ci](https://github.com/zombu4/Greedbay-Detective/actions/workflows/ci.yml/badge.svg)](https://github.com/zombu4/Greedbay-Detective/actions/workflows/ci.yml)

## Building from source

What builds today is the core library (work folder, settings, logging,
single-instance lock, version, database) and its tests. There is no app window
yet.

You need:
- CMake 4.4.4 (the project requires 4.4 or newer)
- Qt 6.12.0 with Qt WebSockets. The code uses Qt Core, Qt Network, Qt SQL,
  Qt WebSockets, Qt Widgets and Qt Test; the exact pinned install is in
  [.github/workflows/ci.yml](.github/workflows/ci.yml).
- A Chromium-based browser for reading eBay pages: Brave, Google Chrome or
  Microsoft Edge (on Linux also Chromium). The app starts it as a separate
  instance with its own profile in the app's work folder.
- macOS and Linux: Ninja (CI uses 1.13.2) and a C++20 compiler (macOS: Xcode)
- Windows: Visual Studio 2026 with the C++ workload and the MSVC v143 (VS 2022)
  build tools; Qt 6.12.0 for Windows is built with MSVC 2022, and the `windows`
  preset compiles with that toolset

Point `QT_ROOT_DIR` at the Qt folder for your system (it contains `bin` and `lib`),
then configure, build and test with the presets. On macOS (the first line assumes
Qt is installed in `$HOME/Qt`; change the path if it is elsewhere):

```sh
export QT_ROOT_DIR="$HOME/Qt/6.12.0/macos"
cmake --preset macos
cmake --build --preset macos-release
ctest --preset macos-release
```

The `macos` preset builds universal binaries (arm64 and x86_64) for macOS 14.4
and newer. For a debug build use `macos-debug` in the last two commands.

On Linux use the presets `linux` and `linux-release`, on Windows `windows` and
`windows-release` (each also has a `-debug` variant). On Windows the tests load
the Qt DLLs from Qt's `bin` folder, so add `%QT_ROOT_DIR%\bin` to `PATH` before
running `ctest`; CI does the same.

CI builds and tests the release presets on macOS (arm64 and x86_64), Windows
and Ubuntu 24.04 on every push and pull request.

Compiler warnings are errors in every build.

## License

No license has been granted yet. All rights reserved. A license will be added
before the first release.

## Contributions

Outside contributions are not being accepted at this time.
