# XBL Cloud Game Save Sync

<img width="1270" height="714" alt="Screenshot 2026-06-02 at 1 37 10 AM" src="https://github.com/user-attachments/assets/e5154cbe-7fdd-40d6-938d-26670ed7f993" />
--
<img width="1256" height="676" alt="Screenshot 2026-06-02 at 1 37 26 AM" src="https://github.com/user-attachments/assets/d1020e20-a473-4d53-af9e-da2952623167" />


A small homebrew application for the **original Xbox** (built with [nxdk](https://github.com/XboxDev/nxdk)) that:

1. Logs in to your Insignia account by showing a **QR code** on screen (OAuth2
   device flow) and obtains a session key.
2. Scans every game save under `E:\UDATA` and reads its metadata.
3. Dumps the console EEPROM and writes the decrypted HDD key to a file.
4. Writes a human-readable report of all saves.
5. Compresses each game's saves individually into a per-game archive.
6. **Uploads** the EEPROM, HDD key and each game's `.dukex` archive to the
   xb.live website, where they appear under a **Game Saves** dashboard
   tab.

All local output is written to a new `E:\GameSaves\` folder. The upload step is
skipped automatically if there is no network or you do not finish logging in.

## End-to-end flow

```
Xbox app                         auth.insigniastats.live        Insignia Stats site
--------                         -----------------------        -------------------
(first run only) POST /api/auth/device ▶ user_code + QR URL
show QR on TV  ◀── you scan with phone, log in on the web page
poll /api/auth/device/token ──▶  sessionKey  ── saved to E:\GameSaves\insignia_session.txt
(later runs)     GET /api/auth/user  ▶ validate saved session (no QR if still valid)
scan E:\UDATA, dump EEPROM/HDD key
GET  /api/me/xbox-saves/manifest      (X-Session-Key) ─────────▶ returns stored fingerprints
   └─ skip titles whose saves are unchanged
POST /api/me/xbox-saves/console-data  (X-Session-Key) ─────────▶ stores EEPROM + encrypted HDD key
POST /api/me/xbox-saves/game          (raw .dukex, new/changed only) ▶ stores each .dukex
GET  /api/me/xbox-saves/download/<id> (titles on server but not local) ◀ pulls .dukex
   └─ unzip into E:\UDATA\<id>\  (restores saves from the server)
                                                                 Dashboard ▸ Game Saves tab
```

## Output files


After running, `E:\GameSaves\` contains:

| File | Contents |
|------|----------|
| `eeprom.bin` | Raw 256-byte EEPROM dump |
| `hdd_key.txt` | Decrypted 16-byte HDD key (hex) and console serial number |
| `saves_report.txt` | Per-title and per-save metadata report |
| `<TitleID>.dukex` | One archive per game, holding that game's saves (`.dukex`) |

Each `<TitleID>.dukex` (e.g. `4D530064.dukex`) is a compressed archive. It contains everything under that game's
`E:\UDATA\<TitleID>\` folder, with entry names relative to it (e.g.
`TitleMeta.xbx`, `001aecf7/SaveMeta.xbx`).

### Example `saves_report.txt`

```
=== Original Xbox Save Scan ===
Date: 2026-06-01 22:00:00
Title folders: 2
Save folders: 3

[TITLE] 4D530064
  TitleName: Halo 2
  Path: E:\UDATA\4D530064
  Files: 5 (204800 bytes)
  [SAVE] 001aecf7
    Name: Campaign
    Files: 3 (102400 bytes)
```

## How it works

### Save metadata

Original Xbox saves live under `E:\UDATA\<TitleID>\`, where `<TitleID>` is an
8-character hex game ID. Two INI-style text files describe them
(see [xboxdevwiki](https://xboxdevwiki.net/Xbox_Savegame_System)):

- `TitleMeta.xbx` in the title folder holds `TitleName=...`.
- `SaveMeta.xbx` in each save subfolder holds `Name=...`.

These files may be plain ASCII or UTF-16 LE with a BOM, and localized variants
repeat the key under `[XX]` sections. The parser in
[`src/parse_meta.c`](src/parse_meta.c) handles both encodings and prefers the
`[default]`/unsectioned value.

### HDD key

The HDD key is **not** stored with saves. It lives in the motherboard EEPROM in
an RC4-encrypted section. Rather than reimplement the decryption, this app reads
the already-decrypted key the kernel exposes as `XboxHDKey` (the kernel decrypts
it from the EEPROM at boot, correctly for every motherboard revision). It still
dumps the raw EEPROM to `eeprom.bin` via `HalReadSMBusValue`, and reads the
console serial number from that dump. See [`src/eeprom_export.c`](src/eeprom_export.c).

### Login, saved sessions & incremental upload

<img width="660" height="1434" alt="image0-3" src="https://github.com/user-attachments/assets/d613f1ec-3d98-4586-8e65-82848dd8a4d6" />

- The QR login and HTTPS client are adapted from `XboxQRCodeLogin`. The device
  flow lives in [`src/net_auth.c`](src/net_auth.c); the TLS transport in
  [`third_party/https_client.c`](third_party/https_client.c) (lwIP sockets +
  mbed TLS, **certificate verification disabled** — demo-grade).
- **Saved login:** after a successful login the session key is written to
  `E:\GameSaves\insignia_session.txt` ([`src/session_store.c`](src/session_store.c)).
  On the next run the app validates it against the Insignia auth API
  (`GET /api/auth/user`) and reuses it — the QR code only reappears if the
  session is missing or no longer valid.
- **Raw binary upload:** [`src/upload.c`](src/upload.c) POSTs the EEPROM (small,
  as JSON) and streams each `.dukex` as a **raw binary body** (no base64
  inflation), so the whole archive only needs to fit in RAM once. Archives
  larger than `UPLOAD_MAX_RAW_BYTES` (32 MB) are skipped to protect the
  console's ~64 MB of memory.
- **Incremental upload:** each title gets a deterministic fingerprint
  ([`titleFingerprintHex`](src/scan_udata.c)) from its saves' file counts, sizes
  and last-write times. Before backing up, the app fetches the server's manifest
  (`GET /api/me/xbox-saves/manifest`); titles whose fingerprint already matches
  are skipped entirely (no re-zip, no re-upload). Only new or changed saves are
  archived and sent.
- **Download / restore:** after uploading, the app pulls down any title that
  exists on the server but **not** on this console (e.g. saves you uploaded from
  another Xbox). Each `.dukex` is downloaded
  ([`https_get_to_file`](third_party/https_client.c) streams it to disk) and
  unzipped ([`src/unzip_export.c`](src/unzip_export.c)) into
  `E:\UDATA\<TitleID>\`. To avoid clobbering newer local saves, a title already
  present locally is never overwritten by the download step.
- The destination host is set by `UPLOAD_HOST` / `UPLOAD_PORT` in
  [`src/main.c`](src/main.c) (default `xb.live:443`).

## Building

You need a working nxdk toolchain. See the
[nxdk build guide](https://github.com/XboxDev/nxdk/wiki/Getting-Started).

1. Clone nxdk into this project (or set `NXDK_DIR` to an existing checkout):

```bash
git clone --recursive https://github.com/XboxDev/nxdk.git nxdk
```

2. Clone mbed TLS (required for the HTTPS login + upload):

```bash
git clone --depth 1 --branch mbedtls-3.6.2 \
  https://github.com/Mbed-TLS/mbedtls.git third_party/mbedtls
```

3. Activate the nxdk environment and build:

```bash
. nxdk/bin/activate
make
```

This produces `bin/default.xbe` (and a `SaveBackup.iso` you can boot in an
emulator). To build against an nxdk located elsewhere:

```bash
make NXDK_DIR=/path/to/nxdk
```

Networking is enabled in the `Makefile` (`NXDK_NET = y`), so the console needs a
wired Ethernet connection with DHCP at runtime.

## Running

This app runs unsigned code, so it requires a **modded original Xbox**
(softmod, modchip, or TSOP flash).

1. Copy `bin/default.xbe` to the console, e.g. `E:\Apps\SaveBackup\default.xbe`
   (FTP works well with dashboards like UnleashX or EvolutionX).
2. Connect the console to your network with an Ethernet cable.
3. Launch it from your dashboard. **The first time**, a **QR code** appears —
   scan it with your phone and finish logging in to Insignia in the browser.
   Subsequent runs reuse the saved login and skip the QR step.
4. The app then scans, backs up, and uploads automatically, printing progress
   on screen. Only new or changed game saves are uploaded; unchanged games are
   skipped. The local copy still lands in `E:\test\`.
5. Open the website, go to **Dashboard ▸ Game Saves**, and your games, EEPROM
   and (password-gated) HDD key are there.

It can also be booted from a burned disc or in [XEMU](https://xemu.app/) using
the generated `.iso` (networking required for the login/upload steps).

## Limitations

- Reads the standard `TitleMeta.xbx` / `SaveMeta.xbx` only; game-specific data
  inside proprietary save files is not parsed.
- Non-save `UDATA` folders (such as dashboard/network config) still appear in
  the report as titles.
- Compressing every game can take several minutes on real hardware..
- Per-game archives are named by Title ID (e.g. `4D530064.dukex`) so the
  filenames are always valid; the readable game name is in `saves_report.txt`.
- Individual files larger than 16 MB are skipped to protect
  console memory; a whole game's `.dukex` larger than 32 MB is skipped by the
  uploader (it is still written locally to `E:\GameSaves\`).
- Assumes a normal (modded) retail kernel with a readable EEPROM.

## Layout

```
.
├── Makefile
├── lwip_override/
│   └── lwipopts.h        # silence lwIP debug (shares the QR framebuffer)
├── src/
│   ├── main.c            # orchestration: login → scan → backup → upload
│   ├── net_auth.c/.h     # Insignia QR device-login + session validation
│   ├── session_store.c/.h# saves/loads the login so the QR is shown only once
│   ├── upload.c/.h        # raw-binary HTTPS upload + incremental manifest
│   ├── base64.c/.h       # base64 encoder (EEPROM + title names)
│   ├── scan_udata.c/.h   # UDATA traversal
│   ├── parse_meta.c/.h   # .xbx metadata parsing
│   ├── eeprom_export.c/.h# EEPROM dump + HDD key
│   ├── report.c/.h       # report writer
│   ├── zip_export.c/.h   # miniz-based zipper
│   └── unzip_export.c/.h # miniz-based unzipper (restore from server)
└── third_party/
    ├── https_client.c/.h # lwIP + mbed TLS HTTPS client
    ├── qrcodegen.c/.h     # Nayuki QR encoder
    ├── nxdk_entropy.c     # mbed TLS entropy hook (demo-grade)
    ├── nxdk_mbedtls_time.c
    ├── mbedtls_nxdk_config.h
    ├── miniz.c / miniz.h
    ├── mbedtls/           # cloned separately (see Building)
    └── MINIZ_LICENSE
```
