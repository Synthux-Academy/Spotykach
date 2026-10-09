# USB Sample Transfer — Specification

Status: draft v1 (MVP)

Spotykach exposes its SD card sample slots to a browser-based file manager over the rear USB-C port. The manager can list slots, upload audio (any format Chrome can decode, converted in the browser), download slots as stored, and delete slots.

This is not USB mass storage. The device switches into a dedicated **transfer mode**, in which the rear port stops being a USB MIDI device and becomes a USB CDC (virtual serial) device. The browser talks to it through Web Serial using the binary protocol below.

## 1. Scope

### In scope (MVP)
- Switch from normal mode to transfer mode, started by the host.
- Show device and card info: firmware version, protocol version, card size and free space.
- List all 36 slots: occupied or empty, original file name, duration, size, cue count, and whether the device can load the file.
- Upload a file into a slot. The browser converts it to 48 kHz / stereo / float32, truncates it to the deck buffer length, and keeps cue points and the original file name. The write is atomic: temp file, CRC check, then rename.
- Download a slot exactly as stored (byte-exact), saved under the original file name.
- Delete a slot.
- Leave transfer mode from the host, from the device, or after an inactivity timeout.
- Chromium-based browsers on macOS, Windows and Linux. The page is hosted on GitHub Pages.

### Out of scope (MVP)
- Moving or swapping slots, editing cue points, waveform display.
- Access to `SK/config.txt`, `SK/MEM`, or whole-card backup and restore.
- Resuming an interrupted transfer. A failed transfer is restarted from scratch.
- Entering transfer mode from the device.
- Transfer mode in `DEBUG` builds, where the rear port carries the logger.
- Keeping name and cue points when a slot is re-saved on the device. This is a known gap in `DeckStorage::save()`, tracked separately (§10).

## 2. Background: current storage model

- **Slots.** There are 6 tapes × 6 slots = 36 slots, shared by both decks. Each slot is a file at `/SK/<T>/<N>.wav`, where `T` is one of `B G P R T Y` (Blue, Green, Pink, Red, Turquoise, Yellow) and `N` is `1`–`6`. The `deck_dir` field in `Card::AudioData` is not used.
- **Occupied slot.** A slot is occupied when `f_stat` succeeds and the file size is greater than 0.
- **Loadable format.** The device loads only RIFF/WAVE files with `AudioFormat = 3` (IEEE float), 2 channels, 48000 Hz and 32 bits per sample.
- **Cue points.** Cue points come from a `cue ` chunk, before or after `data`. The device uses each point's `dwSampleOffset` (offset 20 within the 24-byte point).
- **Maximum length.** The deck buffer is `kSourceMaxSeconds = 42` s, which is 2,016,000 frames or about 16.1 MB of audio. Longer files are truncated on load.
- **Mounting.** The card is mounted on demand and unmounted when both decks are released.
- **File system.** FatFS with long file names, read/write, `f_rename`, `f_unlink` and `f_getfree`. exFAT is disabled, so the card must be FAT32.

## 3. Mode lifecycle

```
            SysEx "enter" (USB MIDI)
  NORMAL ───────────────────────────────▶ TRANSFER
 (USB MIDI)   ack OK, then re-enumerate  (USB CDC)
     ▲                                       │
     └───────────────────────────────────────┘
        EXIT command / Play A or Play B pad /
        10 s with no valid frame
```

### 3.1 Entering
1. The page sends the **enter** SysEx (§4) to the Spotykach USB MIDI output port.
2. The firmware handles it only if it arrived over **USB** MIDI. SysEx on DIN MIDI is ignored.
3. The firmware checks the preconditions, in order:
   - Startup preload is finished, no deck is `loading` or `saving`, and no deck is recording. Otherwise it replies `BUSY` and stays in normal mode.
   - The card can be recognised and mounted, using the same recognise/wait/mount pattern as `Storage::activate` (3 attempts, about 600 ms). Otherwise it replies `NO_CARD` and stays in normal mode.
4. The firmware replies with SysEx `OK`, flushes MIDI TX, and waits at least 20 ms.
5. The firmware enters transfer mode:
   - Any deck in `selecting` is deactivated. On exit, the next `activate()` re-reads the slots, so slot state is always fresh. This meets the "refresh like `_on_audio_saved`" requirement.
   - Both decks are stopped while the audio callback is still running, so the stop command is processed. After that the audio callback outputs silence and skips `CoreUI::tick` and `Core::process`; with `CoreUI::tick` skipped, nothing touches USB MIDI. The TIM5 callback also skips gate input and `Core::prepare`.
   - Stale temp files (`/SK/*/*.tmp`) are deleted.
   - USB MIDI is de-initialised and USB CDC is initialised on `FS_EXTERNAL`.
   - The LEDs show the transfer-mode pattern (§7.5).
6. The host's OS sees the MIDI device disappear and a serial device appear, usually within about 1 s.

### 3.2 Leaving
Any of the following makes the device leave transfer mode:
- An `EXIT` command. The device sends its response first.
- A touch on the **Play A** or **Play B** pad. Both are lit while in transfer mode; all other pads are ignored.
- No valid frame received for **10 s** once the page has connected. While idle, the page sends `PING` every 2 s. Before the first valid frame the limit is **60 s**, so the user has time to pick the port in Chrome's picker on first use.
- DTR detection is not implemented: libDaisy's `CDC_Control_HS` ignores line-state changes, and the timeout covers a closed tab.

On exit the device:
- closes any open file and deletes any temp file;
- unmounts the card;
- de-initialises CDC and re-initialises USB MIDI;
- restores normal audio and UI. Decks stay stopped.

## 4. SysEx trigger

There is no registered manufacturer ID, so messages use the non-commercial ID `0x7D` followed by the ASCII signature `S` `K` (`0x53 0x4B`).

| Direction | Bytes | Meaning |
|---|---|---|
| Host → device | `F0 7D 53 4B 01 F7` | Enter transfer mode |
| Device → host | `F0 7D 53 4B 02 <status> F7` | Enter reply. `status`: `00` OK (switching now), `01` BUSY, `02` NO_CARD |

If the page gets no reply within 3 s, it reports "device did not respond (old firmware?)". The window is 3 s because mounting the card can take about 600 ms before the reply is sent.

## 5. Serial protocol

### 5.1 Transport
- USB CDC ACM, Full Speed. Baud rate and line settings are ignored.
- USB IDs: the existing ST VID/PID `0x0483:0x5740` is used in both modes (see risk §9.2).
- **Strict request/response.** The host sends one request and waits for its response before sending the next. With this rule, the device RX buffer only needs to hold one frame and there is no flow control. Host timeout per request: 5 s.
- All integers are little-endian. All strings are UTF-8 with a length prefix and no NUL.

### 5.2 Frame

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | Magic `0x53 0x4B` (`"SK"`) |
| 2 | 1 | `type`: the command ID; a response is the command ID \| `0x80` |
| 3 | 1 | `seq`: set by the host and increased per request; echoed in the response |
| 4 | 4 | `len`: payload length in bytes, at most `MAX_PAYLOAD` |
| 8 | 4 | `crc`: CRC-32 (IEEE 802.3 / zlib) over bytes 2–7 followed by the payload |
| 12 | len | Payload |

- `MAX_CHUNK` = 32768 (= `Card::kChunk`). `MAX_PAYLOAD` = `MAX_CHUNK` + 16.
- The header is 12 bytes, and data-bearing payloads start with a 4-byte field. Data therefore starts at frame offset 16 and stays 4-byte aligned for SD DMA.
- Every response payload begins with `status: u8` (§5.4).
- **Frame errors** are bad magic, `len > MAX_PAYLOAD`, a CRC mismatch, or a partial frame older than 500 ms. On any of these, the device drops its RX buffer and replies `BAD_FRAME` with `type = 0x80`. That is the response type of command `0x00`, which is reserved for this purpose; `0xFF` can't be used because it is the response to `EXIT` (`0x7F`). The host aborts the current operation; nothing is retried automatically. Bytes that arrive before a magic are discarded, which covers modem probing on Linux.

### 5.3 Commands

`tape` is 0–5 (index into `B G P R T Y`), `slot` is 0–5. Each payload below is listed after the status byte.

| ID | Name | Request payload | Response payload (after status) | Valid in session state |
|---|---|---|---|---|
| `0x00` | — | reserved; never sent | `type 0x80` marks frame errors | — |
| `0x01` | HELLO | — | see below | any (resets the session to idle) |
| `0x02` | PING | — | — | any |
| `0x03` | LIST | — | see below | idle |
| `0x10` | UPLOAD_BEGIN | `tape u8, slot u8, rsv u16, size u32, crc32 u32` | — | idle |
| `0x11` | UPLOAD_DATA | `offset u32, data[≤MAX_CHUNK]` | — | uploading |
| `0x12` | UPLOAD_COMMIT | — | — | uploading |
| `0x13` | UPLOAD_ABORT | — | — | any |
| `0x20` | DOWNLOAD_OPEN | `tape u8, slot u8, rsv u16` | `rsv u8×3, size u32` | idle |
| `0x21` | DOWNLOAD_READ | `offset u32, length u32` (≤ `MAX_CHUNK`) | `rsv u8×3, data[length]` | downloading |
| `0x22` | DOWNLOAD_CLOSE | — | — | any |
| `0x30` | DELETE | `tape u8, slot u8, rsv u16` | — | idle |
| `0x7F` | EXIT | — | — | any |

**HELLO response:**

| Field | Type | Notes |
|---|---|---|
| status | u8 | |
| proto_major, proto_minor | u8, u8 | This spec: `1.0`. The host refuses to work if the major version differs. |
| tapes, slots | u8, u8 | `6, 6` |
| tape_names | 6 × ASCII | `"BGPRTY"` |
| rsv | u8 ×3 | alignment |
| max_chunk | u32 | `MAX_CHUNK` |
| max_frames | u32 | deck buffer length in frames (`SDRAMBuffer::sourceBufferSize()`) |
| max_cues | u16 | `kMaxFileCuePoints` (255): `kMaxSlicePointCount` minus the end slice the device appends on load |
| rsv | u16 | |
| card_total_kib, card_free_kib | u32, u32 | from `f_getfree` |
| fw_version | u8 len + bytes | `kFirmwareVersion` from `config.h`, e.g. `1.3.1` (§7.6) |

**LIST response:** `status u8, count u8` followed by `count` entries, one per slot, ordered by tape then slot:

| Field | Type | Notes |
|---|---|---|
| tape, slot | u8, u8 | |
| flags | u8 | bit0 occupied · bit1 loadable (the format the device accepts) · bit2 RIFF/WAVE parsed OK |
| name_len | u8 | 0 if there is no `INAM`; at most 64 |
| file_size | u32 | bytes |
| frames | u32 | `data` size ÷ `BytePerBloc`; 0 if not parsed |
| sample_rate | u32 | for computing the duration of non-conforming files |
| cue_count | u16 | number of cue points in the file (before or after `data`) |
| channels, bits | u8, u8 | |
| name | name_len bytes | UTF-8 from `LIST/INFO/INAM` |

The duration shown is `frames / sample_rate`.

### 5.4 Status codes

| Code | Name | Meaning |
|---|---|---|
| 0 | OK | |
| 1 | BAD_FRAME | magic, length or CRC error |
| 2 | UNKNOWN_CMD | |
| 3 | BAD_STATE | command not valid in the current session state |
| 4 | BAD_ARG | tape or slot out of range, length > `MAX_CHUNK`, etc. |
| 5 | NO_CARD | card not mounted or lost |
| 6 | IO | FatFS error |
| 7 | NOT_FOUND | slot empty |
| 8 | NO_SPACE | free space < upload size |
| 9 | TOO_LARGE | upload size > `max_frames × 8 + 64 KiB` |
| 10 | BAD_OFFSET | UPLOAD_DATA offset ≠ bytes received so far, or DOWNLOAD_READ beyond EOF |
| 11 | BAD_CRC | whole-file CRC mismatch at commit |
| 12 | BAD_FORMAT | committed file is not a loadable WAV (§6.1) |

### 5.5 Operation flows

**Connect:** `HELLO` → `LIST`.

**Upload:**
1. `UPLOAD_BEGIN`. The device:
   - checks the size limit and free space (the temp file and the old file exist side by side);
   - creates `/SK` and `/SK/<T>` if missing;
   - opens `/SK/<T>/<N>.tmp` with `FA_CREATE_ALWAYS`.
2. `UPLOAD_DATA` with sequential offsets. The device writes each payload with `f_write` and updates a running CRC-32.
3. `UPLOAD_COMMIT`. The device:
   - checks that the received size equals the declared size and the CRC matches;
   - runs `f_close`;
   - validates the temp file's header with `wav_header()` (§6.1);
   - then runs `f_unlink(<N>.wav)` (ignoring "not found") and `f_rename(<N>.tmp, <N>.wav)`.
4. On any error, or on `UPLOAD_ABORT`, the device closes and deletes the temp file. The old slot stays untouched.
5. The host follows with `LIST` to refresh.

The whole-file CRC is checked against the bytes as received. The file is not read back from the card; a read-back would roughly double the upload time.

**Download:** `DOWNLOAD_OPEN` → `DOWNLOAD_READ` until `size` bytes have arrived → `DOWNLOAD_CLOSE`. The frame CRC protects every chunk. The host saves the bytes unchanged.

On macOS the serial tty can drop the tail of a large device-to-host burst when Chrome doesn't drain it fast enough. Uploads aren't affected. `DOWNLOAD_READ` names an absolute offset and length, so the host recovers without any firmware help:
- **Loss detection.** A reply counts as lost when it stops arriving for 300 ms partway through, or doesn't start within 1.5 s.
- **Retry.** The host clears its receive buffer and asks for the same offset again. A late reply to the old request is ignored because its `seq` no longer matches.
- **Read size:**
  - starts at the ceiling remembered from the last download in this browser (`localStorage`), or at `max_chunk` the first time;
  - halves on each lost reply (down to 1 KiB), and the halved size becomes a ceiling;
  - doubles back up to the ceiling after 8 clean reads;
  - tries above the ceiling only after 64 clean reads;
  - the ceiling is saved at the end of every download that completes.
- **Giving up.** The download fails after 8 lost replies in a row.

The browser console logs every lost reply and the read size the download finished with.

**Delete:** `DELETE`, then `LIST`. Deleting an empty slot returns OK.

**Throughput.** Expect roughly 400–800 KB/s, so a full 42 s slot takes about 20–40 s. The 32 KB lockstep round trip (USB FS plus SD latency) is the limit. If this is too slow, double-buffered pipelining can be added later as protocol 1.1.

## 6. File format

### 6.1 Files written by the web app
Chunk order: `RIFF/WAVE`, `fmt `, `LIST`, `data`, `cue `.

- **`fmt `**, 16 bytes: `AudioFormat=3`, `NbrChannels=2`, `SampleRate=48000`, `BytePerSec=384000`, `BytePerBloc=8`, `BitsPerSample=32`. This is the same layout the device writes in `wav_header(size)`.
- **`LIST`** of type `INFO` holding one `INAM` sub-chunk: the original file name in UTF-8, including its extension, at most 64 bytes (cut at a UTF-8 character boundary), NUL-terminated and padded to even length. It is left out if the name is empty. It sits before `data`, so a single 32 KB header read gives the listing.
- **`data`**: interleaved float32 L/R, at most `max_frames` frames.
- **`cue `**: written only if there are cue points. It holds `dwCuePoints` followed by one 24-byte entry per point (`dwName`=index+1, `dwPosition`=offset, `fccChunk='data'`, `dwChunkStart=0`, `dwBlockStart=0`, `dwSampleOffset`=offset in frames). It goes after `data`, the place the device already reads cues from in `Card::read_audio()` and where most DAWs write them.

**Device validation at commit:**
- `wav_header()` succeeds on the first 32 KB;
- the fields match the loadable format (§2);
- `DataSize > 0`.

### 6.2 Conversion in the browser
1. Read the file as an `ArrayBuffer`.
2. **Metadata (WAV sources only).** Walk the RIFF chunks of the *original* file. Read `fmt ` (source sample rate) and `cue ` (each `dwSampleOffset`), whether `cue ` comes before or after `data`. Other source formats contribute no cue points.
3. **Decode** with `new OfflineAudioContext(2, 1, 48000).decodeAudioData(buf)`. Chrome resamples to 48 kHz while decoding. The supported formats are whatever Chrome decodes: WAV, MP3, FLAC, Ogg/Opus/Vorbis, AAC/M4A. AIFF is not supported in the MVP.
4. **Channels.** Mono is copied to both L and R. More than 2 channels: channels 0 and 1 are kept.
5. **Truncate** to `max_frames` (from HELLO) *before* encoding, so no extra bytes are sent. The UI notes that the file was truncated.
6. **Cue points:**
   - rescale: `round(offset × 48000 / sourceRate)`;
   - drop any ≥ truncated length;
   - sort and remove duplicates;
   - keep the first `max_cues`.
7. Encode as in §6.1 and compute the CRC-32 of the whole file.

### 6.3 Download naming
The file is saved as `INAM` with its extension replaced by `.wav`. If there is no `INAM`, it is saved as `<Tape><Slot>.wav`, e.g. `G3.wav`. Characters that are illegal in file names are replaced with `_`.

## 7. Firmware changes

### 7.1 libDaisy (submodule)
No libDaisy changes were needed:
- **MIDI is a mode of libDaisy's CDC class**, selected by the global `usbd_mode`, which is read when the host fetches the configuration descriptor. Switching works as follows:
  - **to CDC:** `UsbHandle::DeInit(FS_EXTERNAL)` (`Hardware::StopUsbMidi`), then `usbd_mode = USBD_MODE_CDC`, then `UsbHandle::Init(FS_EXTERNAL)` and `SetReceiveCallback` (`transfer::Link::start`);
  - **back to MIDI:** `UsbHandle::DeInit(FS_EXTERNAL)`, then `MidiUsbHandler::Init` + `StartReceive` (`Hardware::StartUsbMidi`), which sets `usbd_mode = USBD_MODE_MIDI` itself.
  - A 200 ms pause between deinit and init lets the host register the disconnect.
- **ZLP.** The patched CDC class already sends a zero-length packet when a transfer is a multiple of 64 bytes (`USBD_CDC_DataIn`).
- **TX-busy.** The TX-busy state is read from `hUsbDeviceHS.pClassData->TxState`. USB doesn't use DMA (`dma_enable = DISABLE`), so the buffers can be in SDRAM.
- **RX buffer.** `CDC_Receive_HS` re-arms the endpoint before calling the receive callback with libDaisy's own buffer, so the callback copies the bytes out right away.

### 7.2 `Card` (`src/hw/card.*`)
- Add `State::transfer`. While it is set, the existing load/save paths refuse to run.
- Add streaming primitives for the transfer service:
  - `open_read(path, size&)` and `open_write(path)`;
  - `read(buf, len, &got)`, `write(buf, len)` and `seek(offset)`;
  - `close()`, `remove(path)`, `rename(from, to)`, `make_dir(path)`, `stat(path, size&)` and `space_kib(total&, free&)`.
- `begin_transfer()` / `end_transfer()` switch the state; `buffer()` exposes the 32 KB SDRAM scratch buffer for header reads.

### 7.3 `wav.*`
- Add `wav_info(...)`. It parses `fmt `, `data`, `LIST/INFO/INAM`, and the cue count before `data`, and reports the offset of the end of `data`. It does not change `wav_header()` behaviour.
- Add `wav_cue_count(...)`, which counts cue points in the tail after `data`.
- LIST reads the first 4 KB of each file, and up to 32 KB if `data` isn't found in that. It then reads 8 KB from `data_end`.
- All chunk walkers stop when a chunk size would run past the buffer, so a corrupt size field can't make them loop.

### 7.4 Transfer service (new `src/transfer/`, added to the Makefile `CPP_SOURCES`)
- **`protocol.h`:** constants, command and status enums, little-endian helpers, CRC-32 (table based).
- **`link`:** CDC RX callback (USB ISR) appends to an RX buffer in SDRAM (`MAX_PAYLOAD + 12`, 32-byte aligned). It sets an overflow flag if the buffer fills. Also handles frame parsing and CRC, and builds TX frames in a second SDRAM buffer of the same size.
- **`service`:** the session state machine (`idle`, `uploading`, `downloading`), the command handlers (§5.3), and the 10 s inactivity timer. It runs from the main loop. Each command finishes within one `process()` call; at most 32 KB of SD I/O happens per call.

### 7.5 App, storage, UI, MIDI
- `CoreMIDI::_process_event`: handle `SystemExclusive` from USB MIDI only and pass matching messages to an `on_transfer_request` callback.
- `Storage`:
  - `can_enter_transfer()`: preload done, and no deck loading or saving;
  - `enter_transfer()`: deactivate decks, mount, set `Card::State::transfer`, clean up temp files;
  - `exit_transfer()`.
- `AppImpl`:
  - add a `Mode { normal, transfer }`;
  - in `transfer`, `Loop()` runs `transfer.process()` and a pad check every 20 ms (`CoreUI::process_transfer`) instead of `_ui.process()` and `_storage.process()`;
  - `ProcessAudio` writes silence;
  - `T5Callback` renders the transfer LED pattern: a steady colour in idle and the progress of the current upload or download on the ring. The exact pattern is decided during implementation.
- `Hardware`: owns a `daisy::UsbHandle` for CDC next to `midi_usb`. `#ifndef DEBUG` guards match the existing MIDI ones.

### 7.6 Version
- The firmware version is a constant in `src/core/config.h`, next to the other `static constexpr` values in `namespace spotykach`:
  ```cpp
  static constexpr const char* kFirmwareVersion = "1.3.1";
  ```
  - The format is `MAJOR.MINOR.PATCH`, with no `v` prefix. It is updated by hand in each release commit, alongside the release tag (e.g. tag `v1.3.1` ↔ `"1.3.1"`).
  - The value is `"1.3.1"` until the first release that includes transfer mode, which bumps it.
- `HELLO` returns `kFirmwareVersion` as `fw_version`, and the web app shows it in the header.
- The protocol version (`proto_major`/`proto_minor`) is a separate constant in `src/transfer/`. It changes only when the wire protocol changes.

## 8. Web app

Location: `web/index.html`, a single file of plain HTML/JS/CSS with no dependencies. It is deployed to GitHub Pages manually by `.github/workflows/pages.yml`, which publishes `web/` from a chosen branch, tag or commit (default `uploader`). GitHub only offers "Run workflow" for workflows present on the default branch, so that file has to exist on `main` too; the rest of the code can stay on its branch. Under Settings → Pages, Source must be "GitHub Actions".

### 8.1 Requirements
- A Chromium-based browser with `navigator.serial` and `navigator.requestMIDIAccess`. Otherwise the page shows a "use Chrome or Edge" message.
- A secure context (HTTPS on Pages, or `localhost` during development).

### 8.2 Connection flow
1. The user clicks **Connect**. If a granted serial port is already present (the device is still in transfer mode, e.g. after a page reload), the page opens it straight away and skips the SysEx.
2. The page calls `requestMIDIAccess({ sysex: true })`. Chrome shows a permission prompt on first use.
3. The page picks the output port whose name contains `Spotykach`. If there are several, it asks the user to choose.
4. It sends the enter SysEx and waits for the reply. On `BUSY` or `NO_CARD` it shows the matching error.
5. It waits up to 5 s for the serial device:
   - **Already granted** (`navigator.serial.getPorts()` filtered to the VID/PID): it polls every 250 ms for up to 2.5 s and opens the port as soon as it appears.
   - **First use** (no granted port within 2.5 s): the page calls `requestPort({ filters: [{ usbVendorId: 0x0483, usbProductId: 0x5740 }] })` straight away while the Connect click still counts as a user gesture (`navigator.userActivation.isActive`, about 5 s in Chrome). Otherwise it shows **"Authorise Spotykach serial port"** for a fresh click. If the picker is closed without a selection, the button stays, and the device keeps waiting for up to 60 s. After the first time the step is automatic.
6. It opens the port and sends `HELLO`. If the protocol major version differs, it shows an error and sends `EXIT`.
7. It sends `LIST` and renders the grid.

**Exit transfer mode** sends `EXIT` and closes the port. On closing the tab, the page tries to send `EXIT`; if that doesn't arrive, the device's 10 s timeout takes over. If the port disconnects, the page returns to the disconnected state with a message.

### 8.3 UI
- **Header:**
  - connection state;
  - firmware version and protocol version;
  - card size and free space (bar + numbers);
  - Connect and Exit buttons.
- **Grid:** 6 rows (tapes, labelled and coloured Blue/Green/Pink/Red/Turquoise/Yellow) × 6 columns (slots 1–6). Each cell shows:
  - **Empty:** a muted "empty" label and an upload target.
  - **Occupied:** a distinct filled style, the original name (or `—`), the duration `m:ss.s`, the cue count, and a "not loadable" badge if flag bit1 is clear.
  - **Actions:**
    - Upload: a file picker, or drag a file onto the cell. It replaces an occupied slot without confirmation, since occupancy is already visible.
    - Download and Delete: shown on occupied slots only.
- **Operations:** one at a time; the other actions are disabled while one runs. The active cell shows a progress bar with phases (converting → uploading → verifying, or downloading).
- **Errors:** a dismissible banner with a readable message for each status code.
- **Idle keep-alive:** `PING` every 2 s while connected and no operation is running.

## 9. Risks and open items

1. **Windows MIDI port exclusivity.** With legacy WinMM drivers, only one application can open a MIDI port. If a DAW holds the Spotykach port, Chrome cannot send the SysEx. The page has to say "close the DAW or release the port". There is no fallback on the device in the MVP; adding a device-side gesture later would solve this.
2. **Same VID/PID across the class switch.** Windows may cache the device per VID/PID and mis-bind the driver when the same ID goes from MIDI to CDC. Test on Windows first. Mitigations: a different `bcdDevice` or serial-number string per mode, or getting a free PID from pid.codes (open-source hardware).
3. **Linux setup.** The user must be in the `dialout` (or `uucp`) group for Web Serial. ModemManager may probe the new `ttyACM` device (handled by framing resync; a udev rule with `ID_MM_DEVICE_IGNORE=1` avoids it). The page shows these notes on Linux.
4. **Switching USB MIDI to CDC and back at runtime** has not been tried with libDaisy. Prove it first (§11, step 1).
5. **DAW reconnect.** After transfer mode ends, the MIDI device re-enumerates, and some DAWs need a rescan to see it again.
6. **FAT32 only.** Cards formatted as exFAT (common for SDXC above 32 GB) don't mount. This is an existing limitation.

## 10. Known issues

### 10.1 Fixed together with this feature
Path buffers (stack overflow on every load):
- `Card::init_read_audio` used `char audio_path[11]` for `"/SK/G/1.wav"`, which needs 12 bytes.
- `Card::init_write_audio` used `char tape_dir_path[4]` for `"SK/G"`, which needs 5 bytes.
- `DeckStorage` used `char name[5]` for `"1.wav"`, which needs 6 bytes.

These were found by AddressSanitizer in the host tests. The path buffers are now 16 bytes and the name buffers 8.

Slot layout:
- The tape and slot names and the path helpers moved into `src/memory/slots.h` / `slots.cpp`. `DeckStorage` and the transfer service share them.

Cue points / slices:
- **Limit.** `Card::read_audio()` appended the end slice only when `end_idx < 31`, which capped files at 32 slices. Files can now carry up to `kMaxFileCuePoints` = 255 cue points. The end slice takes the last of the `kMaxSlicePointCount` = 256 entries.
- **Count type.** The cue count was a `uint8_t` and wrapped to 0 at 256 points. It is now `uint16_t` in `Generator`, `Card::AudioData` and `wav.h`.
- **Parsing:**
  - out-of-range points used to leave gaps in the array; points are now stored contiguously and sorted;
  - reads stop at the end of the buffer;
  - an empty tail no longer gets parsed as stale data.
- **Range check before `data`.** A `cue ` chunk placed before `data` is now range-checked in frames, not bytes.
- **Tail seek.** Cue chunks after `data` are now found from the end of the whole `data` chunk. Previously the search started at the end of the part that fit into the buffer, which missed cues in files longer than 42 s.

### 10.2 Not addressed
- `DeckStorage::save()` writes a bare 44-byte header, so re-saving on the device drops `INAM` and cue points.

## 11. Implementation status
Everything in §3–§8 is implemented: firmware (`src/transfer/`, plus changes in `app.cpp`, `Card`, `Storage`, `CoreMIDI`, `CoreUI` and `Hardware`), `web/index.html`, and `.github/workflows/pages.yml`.

### Verified on the host
- The real `service`, `link`, `card`, `wav` and `slots` sources were compiled for macOS with stubs for FatFS (backed by a directory), USB and `System`. They ran under AddressSanitizer/UBSan.
- The page's own protocol and WAV code drove them through a fake serial port. The run covered:
  - `HELLO` and `LIST`;
  - an upload with a UTF-8 name and 255 cues, then a byte-exact download;
  - rejection of a bad CRC (old slot kept, temp removed), a non-WAV file, a bad offset, a wrong state, a bad argument and an unknown command;
  - a corrupted frame, garbage on the line, a 42 s (16.1 MB) round trip, `DELETE` and `EXIT`.
- A file encoded by the page loads through `Card::init_read_audio`/`read_audio` with 255 cues plus the end slice (256 slices, the last at the file end).

### Still to verify on hardware, in this order
1. **The USB switch.** SysEx enter → serial port appears → `EXIT` → MIDI comes back, on macOS, Windows and Linux (risks §9.1–§9.4).
2. **Throughput** and the SD write latency for 32 KB chunks.
3. **LED pattern and exit pads.**
