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
Under active development. Version 0.1.0 is not released yet; the installers
below are built and tested on every push.

[![ci](https://github.com/zombu4/Greedbay-Detective/actions/workflows/ci.yml/badge.svg)](https://github.com/zombu4/Greedbay-Detective/actions/workflows/ci.yml)

## Your eBay account

Greedbay Detective reads eBay in your own browser, in a profile of its own
that you sign in to with your own eBay account. It loads eBay pages only when
you check a listing, one page at a time, with a pause between pages and a
daily limit (both in Settings). It runs in your account, at your own risk:
eBay's rules apply to your account, and using this app with it is your
choice and your responsibility.

Nothing leaves your computer except those eBay page loads: no accounts, no
servers, no tracking.

## Install

Download the installer for your system from the
[Releases](https://github.com/zombu4/Greedbay-Detective/releases) page. The
app needs one of these browsers installed: Brave, Google Chrome or Microsoft
Edge (on Linux also Chromium).

The installers are not signed by Apple or Microsoft, so both systems warn
before the first start. The steps below get past that warning.

### macOS (Apple Silicon)

1. Open `greedbay-detective-<version>-macos-arm64.dmg` and drag
   `GreedbayDetective` to `Applications`.
2. Open the app from `Applications`. macOS stops it with an alert that Apple
   cannot check it for malicious software. Click **Done**.

   [screenshot: macOS alert "Apple cannot check ... for malicious software" - taken at release]

3. Open **System Settings > Privacy & Security** (you may need to scroll
   down to **Security**). Next to the message about `GreedbayDetective`,
   click **Open Anyway**. Apple: "This button is available for about an hour
   after you try to open the app." Confirm with your login password.

   [screenshot: macOS Privacy & Security with Open Anyway - taken at release]

On macOS 15 and later, Control-click > Open no longer opens such an app;
Apple's way is Open Anyway as above.

### Windows

1. Run `greedbay-detective-<version>-windows-x64.exe`.
2. If Windows shows **Windows protected your PC**, click **More info**, then
   **Run anyway**.

   [screenshot: Windows SmartScreen "Windows protected your PC" with More info and Run anyway - taken at release]

3. The setup installs for your user only, without administrator rights, into
   `%LOCALAPPDATA%\Programs\GreedbayDetective`, with a Start menu entry and an
   entry in **Settings > Apps > Installed apps**.

If Smart App Control is on (Windows 11), Windows blocks unsigned apps;
Microsoft: "There is currently no way to bypass Smart App Control protection
for individual apps."

### Ubuntu 24.04

```sh
sudo apt install ./greedbay-detective-<version>-linux-x64.deb
```

apt also installs the system libraries the app needs from Ubuntu. The app is
installed in `/opt/greedbay-detective` and appears in your applications as
**Greedbay Detective**.

   [screenshot: Ubuntu applications with Greedbay Detective - taken at release]

## Where your data is

Everything the app saves is in one work folder: `$HOME/Library/Application
Support/GreedbayDetective` on macOS, `%LOCALAPPDATA%\GreedbayDetective` on
Windows, `$HOME/.local/share/GreedbayDetective` on Linux. That includes the
browser profile the app uses for eBay (your eBay sign-in and cookies), and
the sellers you marked and the seller reviews the app read (in the saved
lookups' database; never uploaded or shared), the shared seller list's last
copy (when you use the shared list), and a downloaded local model (when you
use one). On
macOS the browser also makes `$HOME/Library/Caches/GreedbayDetective`, which
the uninstall removes too. Exports go to your Downloads folder and are yours
to keep.

## Uninstall

Use **Settings > Your data > Uninstall...** in the app. It asks whether to
keep your saved lookups and history:

- **Keep**: the saved lookups (with your marked sellers and the seller
  reviews read), their backups and the brands and models you confirmed stay;
  installing the app again uses them. Everything else the
  app saved is deleted: settings, logs, the app's browser profiles (your eBay
  sign-in and cookies), installed plugins, temporary files, the shared seller
  list's copy and downloaded local models.
- **Delete everything**: the whole work folder goes.

Then, depending on the system:

- **macOS**: the app removes your data as chosen, moves itself to the Trash
  and shows what was removed. Dragging the app to the Trash yourself leaves
  the work folder behind, browser profile included; use Uninstall... instead.
- **Windows**: the app starts its uninstaller, which asks the same question.
  You can also uninstall from **Settings > Apps > Installed apps**. A silent
  uninstall (`Uninstall.exe /S`) keeps your saved lookups and history.
- **Linux**: the app removes your data as chosen, then its package (it asks
  for your password; where that is not possible it shows the command).
  `sudo apt remove greedbay-detective` or `apt purge` removes the program
  only; your work folder stays.

## Shared seller list

Optional, off until you turn it on in **Settings > Shared seller list**:
reports by other users of the app about sellers they bought from, on one
list the list owner runs. Reading and reporting are separate switches.

- The list is downloaded from the Greedbay Detective server over HTTPS; that
  server sees your IP address like any website does and does not store it
  (it logs no addresses, browser names or request details, only hourly
  counts). When the server cannot be reached the app keeps the last copy and
  says the list was not updated.
- The list is signed (Ed25519 and ML-DSA-65, both must hold) by a server key
  that the list owner's master key authorized; the app refuses anything else,
  and any list older than the one it has.
- A report sends only the seller's eBay user id, your reason and your words,
  and the item number and month of your purchase from that seller - found
  first on your own eBay Purchases page, in the app's browser. Your eBay name
  is never sent; the item number stays on the server (the list shows only the
  month), so the seller cannot look you up in their orders. The report is encrypted to the server's key (X25519 and
  ML-KEM-768), inside HTTPS. Before it is sent, the app opens that item's
  eBay page (one more page) and checks it is this seller's item; when it is,
  the list shows the report as "item checked" (a report whose item could not
  be checked is sent and listed without it). "Item checked" is what the
  reporting app found; the server does not check it again. One report per seller; a report
  goes live at once (many reports for one seller in a day wait for the list
  owner); anyone can dispute a report, and the list owner removes reports on
  request.

The server is a separate program: [tools/seller-list-server](tools/seller-list-server/README.md).
Builds without the list owner's keys (`src/sellerlist/data/master.json`) say
the list is not set up.

## Local model

Optional, off until you turn it on in **Settings > Local model**: a small
language model (Qwen3-4B, or Qwen2.5-1.5B for smaller computers, Apache-2.0)
that runs on your computer only - nothing is sent anywhere. Settings shows its
license, download size and memory need before the one-time download from the
maker's own repository; the file is checked (SHA-256) before every use.

- It double-checks the identity of a listing; when it reads it differently
  from the app's rules, the app asks you and shows both readings.
- It looks at the counted results again; a row it doubts goes to "to check",
  where you decide. It never adds a row to the numbers, never sets an
  identity by itself and never changes a price.
- Every answer passes the app's checks first (the model and brand must be in
  the listing, no specifications as models, no made-up brands, ...); each
  lookup says in one line what the model did.
- It runs in a separate process with time limits; if it is missing, slow or
  fails, the lookup goes on with the rules. On x86-64 it needs a processor
  with AVX2 (most made since 2013-2015); Settings says so when it is missing.

## Plugins

Plugins add rules, figures, warnings or file formats. Each is a folder (or
.zip) with `manifest.json` and `main.js`, run in a sandbox: it gets only the
data its permissions name, cannot read files, go online or start programs,
and is stopped after a time limit. Plugin code runs in a separate process,
so a plugin that misbehaves cannot take the app down. A plugin is off until you turn it on in
**Settings > Plugins**, which shows its permissions first. Four examples come
with the app; the plugin API is described in
[src/plugins/PluginHost.h](src/plugins/PluginHost.h) and
[src/plugins/PluginManifest.h](src/plugins/PluginManifest.h).

## Building from source

What builds: the app (lookups, price history, plugins, export, uninstall),
its installer for your system, a developer window for checking the browser
session and a lookup by hand (a tool, not part of the app and not packaged),
and the tests.

You need:
- CMake 4.4.4 (the project requires 4.4 or newer)
- Qt 6.12.0 with Qt WebSockets and Qt Task Tree. The code uses Qt Core (also
  its private ZIP reader, for plugin .zip files), Qt Gui, Qt Network, Qt SQL,
  Qt WebSockets, Qt Qml, Qt Quick, Qt Quick Controls, Qt Widgets (developer
  window only) and Qt Test; the exact pinned install is in
  [.github/workflows/ci.yml](.github/workflows/ci.yml).
- A Chromium-based browser for reading eBay pages: Brave, Google Chrome or
  Microsoft Edge (on Linux also Chromium). The app starts it as a separate
  instance with its own profile in the app's work folder.
- macOS and Linux: Ninja (CI uses 1.13.2) and a C++20 compiler (macOS: Xcode)
- Windows: Visual Studio 2026 with the C++ workload and the MSVC v143 (VS 2022)
  build tools; Qt 6.12.0 for Windows is built with MSVC 2022, and the `windows`
  preset compiles with that toolset
- For the installers: Windows NSIS 3.13; Ubuntu `dpkg-dev` and `file`

Point `QT_ROOT_DIR` at the Qt folder for your system (it contains `bin` and `lib`),
then configure, build and test with the presets. On macOS (the first line assumes
Qt is installed in `$HOME/Qt`; change the path if it is elsewhere):

```sh
export QT_ROOT_DIR="$HOME/Qt/6.12.0/macos"
cmake --preset macos
cmake --build --preset macos-release
ctest --preset macos-release
```

The `macos` preset builds for Apple Silicon (arm64) Macs with macOS 14.4 and
newer; Intel Macs are not supported. For a debug build use `macos-debug` in the
last two commands.

The app is then `build/macos/src/Release/GreedbayDetective.app` (macOS),
`build/linux/src/Release/GreedbayDetective` (Linux) or
`build\windows\src\Release\GreedbayDetective.exe` (Windows).

To make the installer of your system, run `cpack -C Release` in the build
folder (`build/macos`, `build/linux` or `build\windows`): a `.dmg`, a `.deb`
or a setup `.exe`.

On Linux use the presets `linux` and `linux-release`, on Windows `windows` and
`windows-release` (each also has a `-debug` variant). On Windows the tests load
the Qt DLLs from Qt's `bin` folder, so add `%QT_ROOT_DIR%\bin` to `PATH` before
running `ctest`; CI does the same.

CI builds and tests the release presets on macOS 26 (Apple Silicon), Windows
Server 2025 and Ubuntu 24.04 on every push and pull request, then builds each
installer and tests it: install, start, uninstall, and checks that the listed
paths are gone. A `v*` tag publishes a release from those installers, with
SHA-256 checksums and the release notes from [CHANGELOG.md](CHANGELOG.md).

Compiler warnings are errors in every build.

## Fonts

The app's text uses IBM Plex Sans and IBM Plex Mono from IBM's official
release, under the SIL Open Font License 1.1; source, versions and checksums
are in [resources/fonts/README.md](resources/fonts/README.md).

## Libraries

Built into the app from pinned, SHA-256-checked source archives
([cmake/deps.cmake](cmake/deps.cmake)): libsodium 1.0.22 (ISC), liboqs 0.16.0
(MIT, parts under their own terms) and llama.cpp v0.7.0 (MIT). Their license
texts: [resources/licenses/THIRD-PARTY-NOTICES.txt](resources/licenses/THIRD-PARTY-NOTICES.txt),
also in **Settings > Licenses**.

## License

No license has been granted yet. All rights reserved. A license will be added
before the first release.

## Contributions

Outside contributions are not being accepted at this time.
