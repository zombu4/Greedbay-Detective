# Bundled fonts: IBM Plex

The app's text uses IBM Plex Sans (Regular 400, Medium 500, SemiBold 600) and
IBM Plex Mono (Regular 400, Medium 500), built into the program as Qt
resources (`src/CMakeLists.txt`, `src/ui/Fonts.cpp`).

License: SIL Open Font License 1.1, `IBM-Plex-LICENSE.txt` (the release's
`LICENSE.txt`, with its line endings changed from CRLF to LF by this
repository's `.gitattributes`; the text is unchanged). The app shows it in
Settings > Licenses.

## Source

From IBM's official releases at https://github.com/IBM/plex/releases:

| Package | Release tag | Published (UTC) | Asset | SHA-256 of the asset |
|---|---|---|---|---|
| IBM Plex Sans | `@ibm/plex-sans@1.1.0` | 2024-11-13 08:23:59 | `ibm-plex-sans.zip` | `fb365d910566e6d199cc2c15579a7dd9a267128e18431a394ed81f1970c69200` (computed on download; GitHub lists no digest for this asset) |
| IBM Plex Mono | `@ibm/plex-mono@2.5.0` | 2026-06-11 19:24:51 | `ibm-plex-mono.zip` | `6d23f01257663d8cc49a0d64c22ced630b79e0e2a0ac08a0da86e9a38bbc481c` (equal to the digest GitHub lists for the asset) |

Download addresses:

- https://github.com/IBM/plex/releases/download/%40ibm/plex-sans%401.1.0/ibm-plex-sans.zip
- https://github.com/IBM/plex/releases/download/%40ibm/plex-mono%402.5.0/ibm-plex-mono.zip

Files taken from the zips, unchanged:

| File here | Path in the zip |
|---|---|
| `IBMPlexSans-Regular.otf` | `ibm-plex-sans/fonts/complete/otf/IBMPlexSans-Regular.otf` |
| `IBMPlexSans-Medium.otf` | `ibm-plex-sans/fonts/complete/otf/IBMPlexSans-Medium.otf` |
| `IBMPlexSans-SemiBold.otf` | `ibm-plex-sans/fonts/complete/otf/IBMPlexSans-SemiBold.otf` |
| `IBMPlexMono-Regular.otf` | `ibm-plex-mono/fonts/complete/otf/IBMPlexMono-Regular.otf` |
| `IBMPlexMono-Medium.otf` | `ibm-plex-mono/fonts/complete/otf/IBMPlexMono-Medium.otf` |
| `IBM-Plex-LICENSE.txt` | `ibm-plex-sans/LICENSE.txt` (identical to `ibm-plex-mono/LICENSE.txt`) |

The SHA-256 of every file here is in `ci/fonts.sha256`; CI checks them on
every push.
