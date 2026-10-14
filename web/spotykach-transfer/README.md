# @synthuxacademy/spotykach-transfer

Host side of the Spotykach USB sample transfer protocol, for web apps in Chromium-based browsers. It:

- switches the device into transfer mode over USB MIDI (SysEx);
- talks to it over Web Serial: lists, uploads, downloads and deletes the 36 sample slots;
- prepares audio the way the device loads it (48 kHz stereo 32-bit float WAV with the name and cue points).

It lives in the [Spotykach firmware repository](https://github.com/Synthux-Academy/Spotykach) next to the firmware side (`src/transfer/`), so both sides change together. [`docs/usb-transfer.md`](../../docs/usb-transfer.md) describes the protocol.

## Install

```sh
npm install @synthuxacademy/spotykach-transfer
```

It's a single framework-free ES module with TypeScript types and no dependencies. Without a bundler, import `index.js` directly.

## Use

```js
import {
  connect, authorizePort, NeedsAuthorizationError, convertAudio, SLOT_OCCUPIED,
} from "@synthuxacademy/spotykach-transfer";

// From a click
let connection;
try {
  connection = await connect();
} catch (e) {
  if (!(e instanceof NeedsAuthorizationError)) throw e;
  // First use on this computer: Chrome needs a fresh click to grant the serial port
  connection = await authorizePort(); // call this from a button's click handler
}
const { link, info } = connection; // info: firmware, protocol, card space, limits
link.onclose = error => console.log("lost", error.message);

const slots = await link.list(); // 36 entries, flags & SLOT_OCCUPIED for used ones

const { bytes, truncated } = await convertAudio(await file.arrayBuffer(), file.name, info);
await link.upload(tape, slot, bytes, { onProgress: p => console.log(p) });

const wav = await link.download(tape, slot);

// Parts of a file without downloading it all, e.g. the header and the cue chunk
const head = await link.readSlot(tape, slot, async (size, read) => read(0, 4096));

await link.remove(tape, slot);
await link.exit(); // back to USB MIDI
```

`tape` is 0–5 (`info.tapeNames`, `BGPRTY`) and `slot` is 0–5.

### Notes

- **Request order.** Requests run one at a time. Operations made of several requests (upload, download, `readSlot`, `hello`) don't interleave, and `link.exclusive(work)` runs your own sequence the same way.
- **Keep-alive.** The device leaves transfer mode after 10 s without a request. `connect()` starts a keep-alive that pings while the link is otherwise idle.
- **Ranges.** `readSlot` ranges may start anywhere. The device reads only from multiples of `SECTOR_SIZE` (512), so a range is read from the boundary below and trimmed.
- **Recovery.** Uploads resend chunks after a corrupted frame or a lost reply. A frame the device rejected comes back as `DeviceError` with `frameError` set to the reason (`FRAME_ERROR`): checksum mismatch, a request that stopped arriving, an overflowing buffer or an invalid length. Downloads adapt their read size to serial drivers that drop large bursts (macOS); the size that worked is remembered in `localStorage`.
- **Format check.** `upload()` checks the file first (`checkUploadable`): 48 kHz stereo 32-bit float, within `maxFrames` and `maxCues`. `encodeWav` writes files the device reads fastest, with the audio 4-byte aligned.
- **Windows.** Only one app can open a MIDI port at a time, so close the DAW before connecting.
- **Linux.** Web Serial needs the user in the `dialout` group (`uucp` on Arch).

## Versioning

The major version follows the protocol major version (`PROTO_MAJOR`), so `1.x` talks to every firmware with protocol 1.x. `connect()` refuses another major version with `ProtocolMismatchError`.

| Package | Protocol | Firmware |
|---|---|---|
| 1.0.1 | 1.0 | `uploader` branch, first release with USB transfer |

## Development

From the repository root:

```sh
tests/transfer/run.sh
```

This builds the firmware's transfer code for the host (C++17 compiler needed) and runs this package against it with Node 20+, including dropped, cut and corrupted frames.

### Releasing

1. Update `version` in `package.json` and the table above, and commit.
2. Push a tag `spotykach-transfer-v<version>`, e.g. `spotykach-transfer-v1.0.1`.

The publish workflow (`.github/workflows/transfer-publish.yml`) runs the tests, checks that the tag matches `package.json`, and publishes to npm through trusted publishing, with no token. The package's npm settings list this repository and workflow as its trusted publisher.
