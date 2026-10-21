# seller-list-server

The server of the app's shared seller list (DESIGN 3.7): one small program the
list owner runs on a machine of their own. Apps reach it at
`https://<its host name>/` only, with the normal certificate check; the list's
signatures are what the apps trust. It

- takes reports and disputes, each sealed to the server's report key
  (X25519 + ML-KEM-768, XChaCha20-Poly1305) inside HTTPS;
- checks a report: well formed, signed by the sending installation's own key,
  at most one report per installation and seller, at most 5 per installation
  a day, at most 500 a day in all; a seller with 3 or more reports in 24 hours
  has new ones held for the owner. These limits use the installation's
  anonymous report key, never an IP address;
- checks the reported item once on eBay's public item page (one page per
  report, at least 10 seconds apart; after HTTP 403 or 429 it loads nothing
  for an hour): a page whose seller links name the reported seller makes the
  report go live; anything else holds it for the owner;
- signs the list of live reports (Ed25519 + ML-DSA-65) and serves it.

The owner's master key never comes to the server. It signs an authorization of
the server's keys (which the apps check) and, if ever needed, a revocation.

**The item check today.** When the server's request for a sold item's page was
tried (2026-10-20, one request), eBay answered HTTP 403 with its error page.
Until that changes, no report is confirmed automatically: every report waits in
the owner's queue (`queue`, then `publish` or `remove`).

## What it records about the people who use it

Nothing. The server sees each app's IP address, like any website does, and
does not store or log it: no address, browser name (user agent), header, path
or body is written anywhere. Once an hour it prints one line of counts only:

```
last hour: 41 connections, 0 turned away (all slots in use), replies: 200 x40, 202 x1
```

Reports keep what the app sends (seller, reason, text, item number, month,
the installation's report key) in `store.sqlite`; the published list holds the
seller, reason, text, month and "item checked" only.

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
| `tls/fullchain.pem`, `tls/privkey.pem` | copies of the Let's Encrypt certificate and key (step 5) |

## What the server needs

- A Linux machine you run yourself, reachable from the internet.
- A DNS name for it, for example `list.example.org`: an A record with its
  public IPv4 address (and an AAAA record if it has public IPv6).
- TCP port 443 open to the internet, in the machine's firewall and in any
  router in front of it (port forwarding). Port 80 as well while Let's
  Encrypt checks the name with certbot's standalone mode (first issue and
  each renewal).
- Packages: a C and C++ compiler, `make`, Python 3 (for aqtinstall), certbot,
  and the OpenSSL 3 library (Qt's prebuilt Linux libraries load OpenSSL 3 at
  run time for HTTPS). CMake 4.4 and Ninja as in the CI (`.github/workflows/ci.yml`).
- A service user that owns the data folder and nothing else.

## Build

Requirements: CMake 4.4 or newer, a C and C++ compiler, `make` and Ninja, and
Qt 6.12 base (Qt Core, Network and Sql with the SQLite driver). The project's
CI installs Qt with aqtinstall from download.qt.io and checks every archive
against `ci/qt-6.12.0.sha256` (see `.github/workflows/ci.yml`); on Linux:

```sh
sudo python3 -m aqt install-qt linux desktop 6.12.0 linux_gcc_64 --base https://download.qt.io -O /opt/Qt
```

The server alone (no app, no tests; libsodium and liboqs are downloaded and
checked by SHA-256 as in `cmake/deps.cmake`):

```sh
cmake -S . -B build-server -G Ninja -DCMAKE_BUILD_TYPE=Release -DAPP_SERVER_ONLY=ON \
      -DCMAKE_PREFIX_PATH=/opt/Qt/6.12.0/gcc_64
cmake --build build-server
sudo install -D -m 755 build-server/tools/seller-list-server/seller-list-server /opt/seller-list/seller-list-server
```

The program finds Qt in `/opt/Qt`, so leave that folder in place. The same
program, built on the owner's computer, makes and uses the master key (the
normal build has it too: `tools/seller-list-server/seller-list-server` in the
build folder).

## Setting up

The examples use `list.example.org`; use your own name.

1. **The service user and its folder** (on the server):

   ```sh
   sudo useradd --system --home-dir /var/lib/seller-list --create-home --shell /usr/sbin/nologin seller-list
   sudo chmod 700 /var/lib/seller-list
   ```

2. **The server's keys** (on the server):

   ```sh
   sudo -u seller-list /opt/seller-list/seller-list-server server-keys --data /var/lib/seller-list
   ```

   It asks for a new passphrase twice (at least 12 characters). Copy
   `/var/lib/seller-list/server.public.json` to the owner's computer.

3. **The master key** (once, on the owner's computer; keep the folder and its
   passphrase safe and backed up - a lost master key means a new app release):

   ```sh
   seller-list-server master-keys --master ~/seller-list-master
   ```

4. **The authorization and the app's file** (on the owner's computer):

   ```sh
   seller-list-server authorize --master ~/seller-list-master --server-public server.public.json \
       --host list.example.org --sequence 1 --days 365 --out authorization.json
   seller-list-server app-master --master ~/seller-list-master --host list.example.org --out master.json
   ```

   Copy `authorization.json` and `~/seller-list-master/master.public.json` to
   `/var/lib/seller-list/` on the server (owned by `seller-list`). Put
   `master.json` in the app's source as `src/sellerlist/data/master.json`;
   builds made from then on read the list and send reports to
   `https://list.example.org/`. Before the 365 days are over, make a new
   authorization with a higher `--sequence`, copy it over the old one and
   restart the server (it reads its key files when it starts).

5. **The certificate** (on the server). A small script copies the certificate
   and key to the service user's folder each time certbot gets them (its
   `--deploy-hook` runs once for each certificate issued, renewals included;
   `$RENEWED_LINEAGE` is the certificate's `live` folder):

   ```sh
   sudo tee /usr/local/sbin/seller-list-tls >/dev/null <<'EOF'
   #!/bin/sh
   set -e
   install -d -o seller-list -g seller-list -m 700 /var/lib/seller-list/tls
   install -o seller-list -g seller-list -m 600 "$RENEWED_LINEAGE/fullchain.pem" /var/lib/seller-list/tls/fullchain.pem
   install -o seller-list -g seller-list -m 600 "$RENEWED_LINEAGE/privkey.pem" /var/lib/seller-list/tls/privkey.pem
   EOF
   sudo chmod 755 /usr/local/sbin/seller-list-tls
   sudo certbot certonly --standalone -d list.example.org --deploy-hook /usr/local/sbin/seller-list-tls
   ```

   certbot renews the certificate by itself before it expires (Let's Encrypt
   certificates last 90 days). The running server notices the new files
   within two minutes and uses them without a restart; if a renewed
   pair does not load it prints a "problem:" line and keeps the old one.

6. **The passphrase file** (for a start without anyone at the keyboard):

   ```sh
   sudo -u seller-list sh -c 'umask 077; cat > /var/lib/seller-list/passphrase'
   ```

   Type the server's passphrase, Enter, then Ctrl-D. Anyone with access to
   that account or to root can then use the keys; without the file the
   program asks for the passphrase at every start, which needs a terminal.

7. **The service** (systemd; the capability lets the service user listen on
   port 443):

   ```sh
   sudo tee /etc/systemd/system/seller-list.service >/dev/null <<'EOF'
   [Unit]
   Description=Shared seller list server
   After=network-online.target
   Wants=network-online.target

   [Service]
   User=seller-list
   Group=seller-list
   ExecStart=/opt/seller-list/seller-list-server serve --data /var/lib/seller-list --port 443 --tls-cert /var/lib/seller-list/tls/fullchain.pem --tls-key /var/lib/seller-list/tls/privkey.pem --passphrase-file /var/lib/seller-list/passphrase
   AmbientCapabilities=CAP_NET_BIND_SERVICE
   NoNewPrivileges=true
   ProtectSystem=strict
   ReadWritePaths=/var/lib/seller-list
   Restart=on-failure

   [Install]
   WantedBy=multi-user.target
   EOF
   sudo systemctl daemon-reload
   sudo systemctl enable --now seller-list
   journalctl -u seller-list
   ```

   The log shows a `serving HTTPS on ... port 443` line, the published lists
   and the hourly counts. `https://list.example.org/v1/list`
   in a browser shows the signed list.

### With a web server already on port 443

If another web server or reverse proxy already holds port 443, let it do the
HTTPS for `list.example.org` and pass the requests to the program on the same
machine, which then serves plain HTTP on the loopback address only:

```sh
seller-list-server serve --data /var/lib/seller-list --port 8640 --passphrase-file /var/lib/seller-list/passphrase
```

(no `--tls-cert`/`--tls-key`; plain HTTP is refused on any address but
127.0.0.1 and ::1). The proxy must keep no client addresses either: turn its
access log off for that site (nginx: `access_log off;`) and check what its
error log writes. It must send request bodies with `Content-Length` (the
program refuses chunked bodies) and may send at most 16 KiB.

## Moderation

```sh
seller-list-server queue --data /var/lib/seller-list          # held and pending reports, open disputes
seller-list-server publish --data /var/lib/seller-list --id <report>
seller-list-server hold --data /var/lib/seller-list --id <report>    # off the list, kept
seller-list-server remove --data /var/lib/seller-list --id <report>  # off the list for good
seller-list-server close-dispute --data /var/lib/seller-list --id <n>
```

Run them as the service user (`sudo -u seller-list ...`). The running server
signs a new list within a minute of a change. A list's number is at least the
time in seconds, so a server whose database was lost still serves lists the
apps accept.

## If a server key is lost or stolen

On the owner's computer:

```sh
seller-list-server revoke --master ~/seller-list-master --server-public server.public.json \
    --sequence 1 --revocations revocations.json
```

Make new server keys (step 2), authorize them with a higher sequence (step 4),
copy `revocations.json` and the new `authorization.json` to the server, and
restart it. An app refuses every list signed by a revoked key.
