# seller-list-server

The server of the app's shared seller list (DESIGN 3.7): one small program the
list owner runs behind a Tor onion service. Apps reach it only through Tor;
the server never sees an app user's address. It

- takes reports and disputes, each sealed to the server's report key
  (X25519 + ML-KEM-768, XChaCha20-Poly1305);
- checks a report: well formed, signed by the sending installation's own key,
  at most one report per installation and seller, at most 5 per installation
  a day, at most 500 a day in all; a seller with 3 or more reports in 24 hours
  has new ones held for the owner;
- checks the reported item once on eBay's public item page (one page per
  report, at least 10 seconds apart; after HTTP 403 or 429 it loads nothing
  for an hour): a page whose seller links name the reported seller makes the
  report go live; anything else holds it for the owner;
- signs the list of live reports (Ed25519 + ML-DSA-65) and serves it.

The owner's master key never comes to the server. It signs an authorization of
the server's keys (which the apps check) and, if ever needed, a revocation.

## Files

On the owner's own computer (`--master <dir>`):

| File | What |
|---|---|
| `master.secret.json` | the master signing keys, encrypted with the owner's passphrase (Argon2id, 256 MiB) |
| `master.public.json` | the master's public keys |

On the server (`--data <dir>`):

| File | What |
|---|---|
| `keys/server-sign.secret.json`, `keys/server-box.secret.json` | the server's keys, encrypted with its passphrase |
| `server.public.json` | the server's public keys, for the owner to authorize |
| `master.public.json` | a copy of the master's public keys |
| `authorization.json` | the master-signed authorization of the server's keys |
| `revocations.json` | master-signed revocations (only when there are any) |
| `store.sqlite` | reports and disputes (with the installations' report keys, never published) |
| `list.json` | the signed list being served |

## Build

Requirements: CMake 4.4 or newer, a C and C++ compiler, `make` and Ninja, and
Qt 6.12 base (Qt Core, Network and Sql with the SQLite driver). The project's
CI installs Qt with aqtinstall from download.qt.io and checks every archive
against `ci/qt-6.12.0.sha256` (see `.github/workflows/ci.yml`); on Linux:

```sh
python3 -m aqt install-qt linux desktop 6.12.0 linux_gcc_64 --base https://download.qt.io -O "$HOME/Qt"
```

The server alone (no app, no tests; libsodium and liboqs are downloaded and
checked by SHA-256 as in `cmake/deps.cmake`):

```sh
cmake -S . -B build-server -G Ninja -DCMAKE_BUILD_TYPE=Release -DAPP_SERVER_ONLY=ON \
      -DCMAKE_PREFIX_PATH="$HOME/Qt/6.12.0/gcc_64"
cmake --build build-server
# the program: build-server/tools/seller-list-server/seller-list-server
```

The same program, built on the owner's computer, makes and uses the master
key (the normal build has it too: `tools/seller-list-server/seller-list-server`
in the build folder).

## Setting up

1. **The onion service.** Install tor on the server (the Tor Project's own
   package repository) and add to its torrc, with a free local port:

   ```
   HiddenServiceDir /var/lib/tor/seller-list/
   HiddenServicePort 80 127.0.0.1:8640
   ```

   After tor restarts, `/var/lib/tor/seller-list/hostname` holds the onion
   address (56 characters and `.onion`).

2. **The server's keys** (on the server, as the user that will run it):

   ```sh
   seller-list-server server-keys --data /var/lib/seller-list
   ```

   It asks for a new passphrase twice (at least 12 characters). Copy
   `server.public.json` to the owner's computer.

3. **The master key** (once, on the owner's computer; keep the folder and its
   passphrase safe and backed up - a lost master key means a new app release):

   ```sh
   seller-list-server master-keys --master ~/seller-list-master
   ```

4. **The authorization** (on the owner's computer):

   ```sh
   seller-list-server authorize --master ~/seller-list-master --server-public server.public.json \
       --onion <the onion address> --sequence 1 --days 365 --out authorization.json
   ```

   Copy `authorization.json` and `~/seller-list-master/master.public.json` to
   the server's data folder. Before the 365 days are over, make a new one with
   a higher `--sequence`, copy it over the old one and restart the server (it
   reads its key files when it starts).

5. **The app's file** (on the owner's computer):

   ```sh
   seller-list-server app-master --master ~/seller-list-master --onion <the onion address> --out master.json
   ```

   Put it in the app's source as `src/sellerlist/data/master.json`; builds made
   from then on can read the list and send reports.

6. **Run it** (on the server):

   ```sh
   seller-list-server serve --data /var/lib/seller-list --port 8640
   ```

   It asks for the server's passphrase and serves on 127.0.0.1:8640 only.
   Typing the passphrase after every restart keeps the keys unreadable to
   anyone who copies the disk. For an unattended start, `--passphrase-file
   <file>` reads it from a file only its owner can read (`chmod 600`); then
   anyone with access to that account or to root can use the keys.

## Moderation

```sh
seller-list-server queue --data /var/lib/seller-list          # held and pending reports, open disputes
seller-list-server publish --data /var/lib/seller-list --id <report>
seller-list-server hold --data /var/lib/seller-list --id <report>    # off the list, kept
seller-list-server remove --data /var/lib/seller-list --id <report>  # off the list for good
seller-list-server close-dispute --data /var/lib/seller-list --id <n>
```

The running server signs a new list within a minute of a change. A list's
number is at least the time in seconds, so a server whose database was lost
still serves lists the apps accept.

## If a server key is lost or stolen

On the owner's computer:

```sh
seller-list-server revoke --master ~/seller-list-master --server-public server.public.json \
    --sequence 1 --revocations revocations.json
```

Make new server keys (step 2), authorize them with a higher sequence (step 4),
copy `revocations.json` and the new `authorization.json` to the server, and
restart it. An app refuses every list signed by a revoked key.
