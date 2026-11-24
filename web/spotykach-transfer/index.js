/**
 * Spotykach USB transfer protocol, host side.
 *
 * Switches the device into transfer mode over USB MIDI (SysEx), talks to it over
 * Web Serial, and prepares WAV files the way the device loads them. Framework-free
 * ES module; the firmware side lives in src/transfer/ of the same repository, the
 * protocol is described in docs/usb-transfer.md.
 *
 * The frames, the SysEx switch, loss recovery and connecting are the shared part,
 * @synthuxacademy/usb-transfer (lib/usb-transfer); this module adds Spotykach's
 * commands, settings and WAV handling, with the same API as before.
 *
 * MIT License, Copyright (c) 2026 Synthux Academy
 */

import {
  defineDevice,
  Link,
  CMD as BASE_CMD,
  STATUS as BASE_STATUS,
  FRAME_ERROR,
  DeviceError,
  TimeoutError,
  NeedsAuthorizationError,
  MidiPortNotFoundError,
  ProtocolMismatchError,
  crc32,
  isSupported,
  grantedPort as baseGrantedPort,
  enterTransferMode as baseEnterTransferMode,
  handshake as baseHandshake,
  connect as baseConnect,
  authorizePort as baseAuthorizePort,
} from "@synthuxacademy/usb-transfer";

export {
  FRAME_ERROR, DeviceError, TimeoutError, NeedsAuthorizationError, MidiPortNotFoundError,
  ProtocolMismatchError, crc32, isSupported,
};

// Protocol ///////////////////////////////////////////////////////////////////

export const USB_VENDOR_ID = 0x0483;
export const USB_PRODUCT_ID = 0x5740;

/** Protocol version this module speaks; the package major version follows the protocol major. */
export const PROTO_MAJOR = 1;
export const PROTO_MINOR = 2;

const MAX_PAYLOAD = 32768 + 16;

export const CMD = Object.freeze({
  hello: BASE_CMD.hello,
  ping: BASE_CMD.ping,
  list: 0x03,
  uploadBegin: 0x10,
  uploadData: 0x11,
  uploadCommit: 0x12,
  uploadAbort: 0x13,
  downloadOpen: 0x20,
  downloadRead: 0x21,
  downloadClose: 0x22,
  remove: 0x30,
  configRead: 0x40,
  configWrite: 0x41,
  fileRead: 0x42,
  fileWrite: 0x43,
  exit: BASE_CMD.exit,
});

export const STATUS = Object.freeze({
  ...BASE_STATUS,
  noCard: 5,
  io: 6,
  notFound: 7,
  noSpace: 8,
  tooLarge: 9,
  badOffset: 10,
  badCrc: 11,
  badFormat: 12,
});

/** English messages per status, index = status code. */
export const STATUS_TEXT = Object.freeze([
  "OK",
  "Transfer error (corrupted frame). Please retry.",
  "Spotykach doesn't know this command. Update its firmware.",
  "Spotykach is busy with another operation.",
  "Invalid request.",
  "SD card not available. Check the card and reconnect.",
  "SD card read/write error.",
  "The slot is empty.",
  "Not enough free space on the SD card.",
  "File is too large.",
  "Transfer got out of sync. Please retry.",
  "Upload was corrupted (checksum mismatch). Please retry.",
  "Spotykach rejected the file format.",
]);

/** DOWNLOAD_READ offsets must be multiples of this; `readSlot` ranges are aligned for you. */
export const SECTOR_SIZE = 512;

/** LIST entry flags */
export const SLOT_OCCUPIED = 1 << 0;
export const SLOT_LOADABLE = 1 << 1;
export const SLOT_PARSED = 1 << 2;

/** The only format the device loads */
export const DEVICE_FORMAT = Object.freeze({ sampleRate: 48000, channels: 2, bitsPerSample: 32, audioFormat: 3 });

const slotArgs = (tape, slot) => new Uint8Array([tape, slot, 0, 0]);

function parseHello(p) {
  const dv = new DataView(p.buffer, p.byteOffset, p.byteLength);
  const decoder = new TextDecoder();
  return {
    protoMajor: p[1],
    protoMinor: p[2],
    tapes: p[3],
    slots: p[4],
    tapeNames: decoder.decode(p.subarray(5, 11)),
    maxChunk: dv.getUint32(14, true),
    maxFrames: dv.getUint32(18, true),
    maxCues: dv.getUint16(22, true),
    totalKiB: dv.getUint32(26, true),
    freeKiB: dv.getUint32(30, true),
    firmware: decoder.decode(p.subarray(35, 35 + p[34])),
  };
}

// Link ///////////////////////////////////////////////////////////////////////

/**
 * One open serial connection to a Spotykach in transfer mode.
 *
 * Requests run strictly one at a time. Operations made of several requests
 * (an upload, a download, HELLO which resets the device session) also run one
 * at a time, through `exclusive()`.
 */
export class SkLink extends Link {
  /** Use `SkLink.open(port)`. */
  constructor(port, device = DEVICE) {
    super(port, device);
  }

  /**
   * Opens the port, retrying while the device is still enumerating. Doesn't talk to it yet.
   * `handshake` calls `open(port, device, { fresh })`: the options go on, so a port that
   * just appeared gets the longer Linux wait.
   */
  static open(port, _device, options) {
    return super.open(port, DEVICE, options);
  }

  /** All slots, empty ones included (flag SLOT_OCCUPIED clear). */
  list() {
    return this.exclusive(async () => {
      const p = await this.request(CMD.list, undefined, 15000);
      const dv = new DataView(p.buffer, p.byteOffset, p.byteLength);
      const decoder = new TextDecoder();
      const slots = [];
      let o = 2;
      for (let i = 0; i < p[1]; i++) {
        const nameLength = p[o + 3];
        slots.push({
          tape: p[o],
          slot: p[o + 1],
          flags: p[o + 2],
          fileSize: dv.getUint32(o + 4, true),
          frames: dv.getUint32(o + 8, true),
          sampleRate: dv.getUint32(o + 12, true),
          cues: dv.getUint16(o + 16, true),
          channels: p[o + 18],
          bits: p[o + 19],
          name: decoder.decode(p.subarray(o + 20, o + 20 + nameLength)),
        });
        o += 20 + nameLength;
      }
      return slots;
    });
  }

  _maxChunk() {
    if (!this.info) throw new Error("Call hello() first.");
    return this.info.maxChunk;
  }

  /**
   * Writes a WAV to the slot. The device stores it as a temp file and replaces the
   * slot only after the CRC and header check, so a failed upload keeps the old one.
   * Checks the file first (see `checkUploadable`).
   */
  upload(tape, slot, bytes, options = {}) {
    const { onProgress, signal } = options;
    return this.exclusive(async () => {
      const maxChunk = this._maxChunk();
      const problems = checkUploadable(bytes, this.info);
      if (problems.length) throw new Error(`Spotykach can't load this file: ${problems.join(" ")}`);

      signal?.throwIfAborted();
      const begin = new Uint8Array(12);
      const dv = new DataView(begin.buffer);
      begin.set(slotArgs(tape, slot));
      dv.setUint32(4, bytes.length, true);
      dv.setUint32(8, crc32(bytes), true);
      await this.request(CMD.uploadBegin, begin, 10000);
      try {
        for (let offset = 0; offset < bytes.length; offset += maxChunk) {
          signal?.throwIfAborted();
          const chunk = bytes.subarray(offset, Math.min(offset + maxChunk, bytes.length));
          const payload = new Uint8Array(4 + chunk.length);
          new DataView(payload.buffer).setUint32(0, offset, true);
          payload.set(chunk, 4);
          await this._sendChunk(payload, offset, chunk.length);
          onProgress?.((offset + chunk.length) / bytes.length);
        }
        signal?.throwIfAborted();
        await this.request(CMD.uploadCommit, undefined, 10000);
      } catch (e) {
        if (!this.closed) {
          try { await this.request(CMD.uploadAbort); } catch (_) { /* the device drops temp files on exit */ }
        }
        throw e;
      }
    });
  }

  /** One UPLOAD_DATA, a chunked write with retries (Link.writeChunk). */
  _sendChunk(payload, offset, length) {
    return this.writeChunk(CMD.uploadData, payload, offset, length);
  }

  /**
   * Opens the slot for reading, hands `work` its size and a range reader, and
   * closes it again. Reads retry with an adapting size when replies get lost.
   * Ranges may start anywhere: the device only reads from multiples of
   * SECTOR_SIZE, so a range is read from the boundary below and trimmed.
   */
  readSlot(tape, slot, work) {
    return this.exclusive(async () => {
      const maxChunk = this._maxChunk();
      const open = await this.request(CMD.downloadOpen, slotArgs(tape, slot));
      const size = new DataView(open.buffer, open.byteOffset).getUint32(4, true);
      const read = async (offset, length, options) => {
        const head = offset % SECTOR_SIZE;
        const bytes = await this._readRange(offset - head, length + head, maxChunk, options);
        return bytes.subarray(head);
      };
      try {
        return await work(size, read);
      } finally {
        if (!this.closed) {
          try { await this.request(CMD.downloadClose); } catch (e) { /* the next HELLO resets the session */ }
        }
      }
    });
  }

  /** The whole slot, byte-exact. */
  download(tape, slot, options = {}) {
    return this.readSlot(tape, slot, (size, read) => read(0, size, options));
  }

  /** DOWNLOAD_READs, ranged reads with an adapting size (Link.readRange). */
  _readRange(start, length, maxChunk, options = {}) {
    return this.readRange(CMD.downloadRead, start, length, maxChunk, options);
  }

  async remove(tape, slot) {
    await this.exclusive(() => this.request(CMD.remove, slotArgs(tape, slot)));
  }

  /** Protocol 1.1 and up: `readConfig` / `writeConfig`. */
  hasConfig() {
    return !!this.info && this.info.protoMinor >= 1;
  }

  /** The text of `SK/config.txt`, null if the card has none. See `parseConfig`. */
  readConfig() {
    return this.exclusive(async () => {
      try {
        const p = await this.request(CMD.configRead);
        return new TextDecoder().decode(p.subarray(4));
      } catch (e) {
        if (e instanceof DeviceError && e.status === STATUS.notFound) return null;
        throw e;
      }
    });
  }

  /**
   * Replaces `SK/config.txt`; a failed write keeps the old file. The device reads
   * it on power-on, so the settings apply from the next start. See `formatConfig`.
   */
  writeConfig(text) {
    return this.exclusive(async () => {
      const bytes = new TextEncoder().encode(text);
      if (bytes.length > this._maxChunk()) throw new Error(`The settings file is larger than ${this._maxChunk()} bytes.`);
      const payload = new Uint8Array(4 + bytes.length);
      payload.set(bytes, 4);
      await this.request(CMD.configWrite, payload, 10000);
    });
  }

  /** Protocol 1.2 and up: `readFile` / `writeFile`. */
  hasFiles() {
    return !!this.info && this.info.protoMinor >= 2;
  }

  /**
   * The text of a small file on the card, null if there is none. `name` is
   * `NAME.EXT` or `SK/NAME.EXT` with a .txt, .md or .json extension (see `isFileName`).
   */
  readFile(name) {
    return this.exclusive(async () => {
      try {
        const p = await this.request(CMD.fileRead, fileNameField(name));
        return new TextDecoder().decode(p.subarray(4));
      } catch (e) {
        if (e instanceof DeviceError && e.status === STATUS.notFound) return null;
        throw e;
      }
    });
  }

  /** Creates or replaces a small file on the card; a failed write keeps the old one. */
  writeFile(name, text) {
    return this.exclusive(async () => {
      const field = fileNameField(name);
      const bytes = new TextEncoder().encode(text);
      if (bytes.length > MAX_FILE_SIZE) throw new Error(`${name} is larger than ${MAX_FILE_SIZE} bytes.`);
      const payload = new Uint8Array(FILE_NAME_SIZE + bytes.length);
      payload.set(field);
      payload.set(bytes, FILE_NAME_SIZE);
      await this.request(CMD.fileWrite, payload, 10000);
    });
  }
}

/** Spotykach as @synthuxacademy/usb-transfer sees it. */
export const DEVICE = defineDevice({
  name: "Spotykach",
  magic: "SK",
  protoMajor: PROTO_MAJOR,
  usbVendorId: USB_VENDOR_ID,
  usbProductId: USB_PRODUCT_ID,
  midiPortName: /spotykach/i,
  connectHint: "Is it connected through the rear USB-C port?",
  maxPayload: MAX_PAYLOAD,
  statusText: STATUS_TEXT,
  enterText: {
    1: "Spotykach is busy (loading, saving or recording). Finish that and try again.",
    2: "No SD card found in Spotykach.",
  },
  // Mounting the card can take ~600 ms before the device replies
  enterReplyMs: [2000, 3000],
  badOffsetStatus: STATUS.badOffset,
  parseHello,
  storageKey: "spotykach",
  logTag: "spotykach",
  Link: SkLink,
});

// Files //////////////////////////////////////////////////////////////////////

/** The path field that starts a FILE_READ / FILE_WRITE request, NUL-padded. */
const FILE_NAME_SIZE = 32;
/** The largest file `readFile` / `writeFile` handle: one frame, less the path field. */
export const MAX_FILE_SIZE = 32768 - FILE_NAME_SIZE;

/**
 * Whether the device reads and writes this file: `NAME.EXT` at the card root or
 * `SK/NAME.EXT`, with letters, digits, `_`, `-` and `.` in the name and a .txt, .md
 * or .json extension. The firmware checks the same; this only fails sooner.
 */
export function isFileName(name) {
  return typeof name === "string"
    && name.length < FILE_NAME_SIZE
    && /^(SK\/)?[A-Za-z0-9_-][A-Za-z0-9_.-]*\.[A-Za-z]+$/.test(name)
    && ["txt", "md", "json"].includes(name.slice(name.lastIndexOf(".") + 1).toLowerCase());
}

function fileNameField(name) {
  if (!isFileName(name)) throw new Error(`Spotykach doesn't read or write "${name}": use NAME.txt, .md or .json, at the card root or in SK/.`);
  const field = new Uint8Array(FILE_NAME_SIZE);
  field.set(new TextEncoder().encode(name));
  return field;
}

// Settings ///////////////////////////////////////////////////////////////////

/**
 * The keys of `SK/config.txt` the firmware knows, with the value it uses when a
 * key is missing. The file is a key line followed by a value line (an integer).
 */
export const CONFIG_KEYS = Object.freeze({
  mid_ch_a: { min: 1, max: 16, default: 1 },  // MIDI channel of deck A
  mid_ch_b: { min: 1, max: 16, default: 2 },
  mid_ps_a: { min: 0, max: 1, default: 0 },   // MIDI Start/Stop starts/stops deck A
  mid_ps_b: { min: 0, max: 1, default: 0 },
  pre_load: { min: 0, max: 1, default: 1 },   // load the recent files on power-on
  slc_mn_a: { min: 0, max: 1, default: 0 },   // deck A slice mode is monophonic
  slc_mn_b: { min: 0, max: 1, default: 0 },
  cue_sm_a: { min: 0, max: 2, default: 0 },   // deck A Size and cue points, see CUE_SIZE_MODE
  cue_sm_b: { min: 0, max: 2, default: 0 },
  grt_mx_a: { min: 0, max: 10, default: 3 },  // deck A initial Grit mix, tenths
  grt_mx_b: { min: 0, max: 10, default: 3 },
});

/** `cue_sm_a` / `cue_sm_b` values */
export const CUE_SIZE_MODE = Object.freeze({
  ignore: 0, // Size ignores cue points
  snap: 1,   // Size snaps to cue points, Alt+Size is free
  free: 2,   // Size is free, Alt+Size snaps to cue points
});

const CONFIG_KEY_LENGTH = 8; // the device compares the first 8 characters, spaces removed
const isConfigNumber = line => /^-?[0-9]*$/.test(line) && line.length > 0;

/* The file's lines the way the device reads them: spaces dropped, empty lines skipped. */
function configLines(text) {
  return (text || "").split(/\r\n|\r|\n/).map(raw => {
    const line = raw.replace(/ /g, "");
    return { raw, line, isValue: isConfigNumber(line), key: line.slice(0, CONFIG_KEY_LENGTH) };
  });
}

/**
 * The settings in `SK/config.txt` as `{ key: value }` for every CONFIG_KEYS key,
 * read like the device does; missing keys get their default. Values outside a
 * key's range are clamped, the way the settings page shows them.
 */
export function parseConfig(text) {
  const values = Object.fromEntries(Object.entries(CONFIG_KEYS).map(([key, spec]) => [key, spec.default]));
  let key = null;
  for (const { line, isValue, key: lineKey } of configLines(text)) {
    if (!line) continue;
    if (!isValue) { key = lineKey; continue; }
    const spec = Object.hasOwn(CONFIG_KEYS, key) ? CONFIG_KEYS[key] : null;
    if (spec) values[key] = Math.min(spec.max, Math.max(spec.min, parseInt(line, 10) || 0));
  }
  return values;
}

/**
 * `text` (the current file, or null) with the given settings written into it:
 * known keys get their value in place, missing ones are appended, and other
 * lines (unknown keys, notes) stay as they were.
 */
export function formatConfig(values, text = null) {
  const lines = configLines(text);
  const eol = /\r\n/.test(text || "") ? "\r\n" : "\n";
  const isKnown = key => Object.hasOwn(CONFIG_KEYS, key) && Object.hasOwn(values, key);
  const written = new Set();
  const out = [];
  let key = null;
  for (const { raw, line, isValue, key: lineKey } of lines) {
    if (line && !isValue) key = lineKey;
    if (line && isValue && isKnown(key)) {
      // Repeated value lines: the device takes the last one, write it once
      if (!written.has(key)) out.push(String(values[key]));
      written.add(key);
      continue;
    }
    out.push(raw);
  }
  while (out.length && out[out.length - 1] === "") out.pop();
  for (const key of Object.keys(CONFIG_KEYS)) {
    if (isKnown(key) && !written.has(key)) out.push(key, String(values[key]));
  }
  return out.join(eol) + eol;
}

// Connecting /////////////////////////////////////////////////////////////////

/** The serial port this browser already has permission for, present while the device is in transfer mode. */
export function grantedPort() {
  return baseGrantedPort(DEVICE);
}

/**
 * Asks the device over USB MIDI to switch to transfer mode and waits for its reply.
 * `onStep` gets a line for each step, for diagnostics.
 */
export function enterTransferMode(options = {}) {
  return baseEnterTransferMode(DEVICE, options);
}

/** Opens the port and checks HELLO. Resolves to `{ link, info }`, with the keep-alive running. */
export function handshake(port, options = {}) {
  return baseHandshake(DEVICE, port, options);
}

/**
 * Connects to a Spotykach: reuses a port that's already in transfer mode (e.g. after
 * a reload), otherwise switches the device and waits for its serial port.
 * On first use, once the click that started this has expired, throws
 * `NeedsAuthorizationError`: call `authorizePort()` from a fresh click.
 * `onStep` gets a line for each step, for diagnostics.
 */
export function connect(options = {}) {
  return baseConnect(DEVICE, options);
}

/** Shows the browser's port picker, so it must run from a click. The device waits up to 60 s for it. */
export function authorizePort(options = {}) {
  return baseAuthorizePort(DEVICE, options);
}

// WAV ////////////////////////////////////////////////////////////////////////

const fourcc = (b, o) => String.fromCharCode(b[o], b[o + 1], b[o + 2], b[o + 3]);

/**
 * Parses a RIFF/WAVE file's chunks: format, where the audio is, cue points
 * (`dwSampleOffset`, before or after the audio) and the `LIST/INFO/INAM` name.
 * Returns null if it isn't RIFF/WAVE.
 */
export function inspectWav(bytes) {
  if (bytes.length < 12 || fourcc(bytes, 0) !== "RIFF" || fourcc(bytes, 8) !== "WAVE") return null;
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const info = {
    audioFormat: 0, channels: 0, sampleRate: 0, bitsPerSample: 0, blockAlign: 0,
    dataOffset: -1, dataSize: 0, frames: 0, cues: [], name: "",
  };
  let o = 12;
  while (o + 8 <= bytes.length) {
    const id = fourcc(bytes, o);
    const size = dv.getUint32(o + 4, true);
    const body = o + 8;
    if (id === "fmt " && body + 16 <= bytes.length) {
      info.audioFormat = dv.getUint16(body, true);
      info.channels = dv.getUint16(body + 2, true);
      info.sampleRate = dv.getUint32(body + 4, true);
      info.blockAlign = dv.getUint16(body + 12, true);
      info.bitsPerSample = dv.getUint16(body + 14, true);
    }
    else if (id === "data") {
      info.dataOffset = body;
      info.dataSize = size;
    }
    else if (id === "cue " && body + 4 <= bytes.length) {
      const count = dv.getUint32(body, true);
      for (let i = 0; i < count; i++) {
        const at = body + 4 + i * 24 + 20;
        if (at + 4 > bytes.length) break;
        info.cues.push(dv.getUint32(at, true));
      }
    }
    else if (id === "LIST" && body + 4 <= bytes.length && fourcc(bytes, body) === "INFO") {
      const end = Math.min(body + size, bytes.length);
      for (let s = body + 4; s + 8 <= end;) {
        const subSize = dv.getUint32(s + 4, true);
        if (fourcc(bytes, s) === "INAM") {
          let text = bytes.subarray(s + 8, Math.min(s + 8 + subSize, end));
          let length = text.length;
          while (length > 0 && text[length - 1] === 0) length--;
          info.name = new TextDecoder().decode(text.subarray(0, length));
        }
        s += 8 + subSize + (subSize % 2);
      }
    }
    o = body + size + (size % 2);
  }
  if (info.blockAlign) info.frames = Math.floor(info.dataSize / info.blockAlign);
  return info;
}

/**
 * Why the device wouldn't take this file, as a list of sentences; empty if fine.
 * `limits` is the HELLO info (`maxFrames`, `maxCues`); the device truncates audio
 * longer than `maxFrames` on load, but refuses uploads well beyond it.
 */
export function checkUploadable(bytes, limits) {
  const info = inspectWav(bytes);
  if (!info) return ["It isn't a WAV file."];
  const problems = [];
  const f = DEVICE_FORMAT;
  if (info.audioFormat !== f.audioFormat || info.channels !== f.channels
      || info.sampleRate !== f.sampleRate || info.bitsPerSample !== f.bitsPerSample) {
    problems.push(`It must be ${f.sampleRate / 1000} kHz stereo 32-bit float (it's ${info.sampleRate} Hz, ${info.channels} ch, ${info.bitsPerSample}-bit, format ${info.audioFormat}).`);
  }
  if (info.dataOffset < 0 || info.dataSize === 0) problems.push("It has no audio.");
  if (limits) {
    if (info.frames > limits.maxFrames) problems.push(`It's longer than ${limits.maxFrames} frames; encode it truncated.`);
    if (info.cues.length > limits.maxCues) problems.push(`It has more than ${limits.maxCues} cue points.`);
  }
  return problems;
}

function utf8Truncate(text, maxBytes) {
  const encoder = new TextEncoder();
  let bytes = encoder.encode(text);
  while (bytes.length > maxBytes) {
    text = text.slice(0, -1);
    bytes = encoder.encode(text);
  }
  return bytes;
}

/**
 * Encodes audio the way the device loads it: 48 kHz stereo 32-bit float, the name
 * in LIST/INFO/INAM, the cue points after the audio.
 * The name is NUL-padded to a multiple of 4 so the audio starts 4-byte aligned
 * (header = 64 + INAM size), which keeps the device's SD reads on the fast path.
 */
export function encodeWav({ left, right, cues = [], name = "" }) {
  const nameBytes = utf8Truncate(name, 64);
  const frames = left.length;
  const dataSize = frames * 8;
  const inamSize = nameBytes.length ? (nameBytes.length + 1 + 3) & ~3 : 0;
  const listSize = inamSize ? 8 + 4 + 8 + inamSize : 0;
  const cueSize = cues.length ? 8 + 4 + cues.length * 24 : 0;
  const total = 12 + 24 + listSize + 8 + dataSize + cueSize;

  const out = new Uint8Array(total);
  const dv = new DataView(out.buffer);
  const id = (o, s) => { for (let i = 0; i < 4; i++) out[o + i] = s.charCodeAt(i); };
  let o = 0;
  id(o, "RIFF"); dv.setUint32(o + 4, total - 8, true); id(o + 8, "WAVE"); o += 12;

  id(o, "fmt "); dv.setUint32(o + 4, 16, true);
  dv.setUint16(o + 8, DEVICE_FORMAT.audioFormat, true);
  dv.setUint16(o + 10, DEVICE_FORMAT.channels, true);
  dv.setUint32(o + 12, DEVICE_FORMAT.sampleRate, true);
  dv.setUint32(o + 16, DEVICE_FORMAT.sampleRate * 8, true);
  dv.setUint16(o + 20, 8, true);
  dv.setUint16(o + 22, DEVICE_FORMAT.bitsPerSample, true);
  o += 24;

  if (listSize) {
    id(o, "LIST"); dv.setUint32(o + 4, listSize - 8, true); id(o + 8, "INFO");
    id(o + 12, "INAM"); dv.setUint32(o + 16, inamSize, true);
    out.set(nameBytes, o + 20); // the NUL terminator and padding are already zero
    o += listSize;
  }

  id(o, "data"); dv.setUint32(o + 4, dataSize, true); o += 8;
  for (let i = 0; i < frames; i++) {
    dv.setFloat32(o, left[i], true);
    dv.setFloat32(o + 4, right[i], true);
    o += 8;
  }

  if (cueSize) {
    id(o, "cue "); dv.setUint32(o + 4, cueSize - 8, true); dv.setUint32(o + 8, cues.length, true);
    o += 12;
    cues.forEach((offset, i) => {
      dv.setUint32(o, i + 1, true);
      dv.setUint32(o + 4, offset, true);
      id(o + 8, "data");
      dv.setUint32(o + 12, 0, true);
      dv.setUint32(o + 16, 0, true);
      dv.setUint32(o + 20, offset, true);
      o += 24;
    });
  }
  return out;
}

/**
 * Converts any audio the browser can decode (WAV, MP3, FLAC, Ogg, AAC/M4A) for the
 * device: resampled to 48 kHz, mono duplicated to stereo, truncated to `maxFrames`.
 * Cue points of a WAV source are rescaled, sorted, de-duplicated and capped to
 * `maxCues`. Browser only (OfflineAudioContext).
 */
export async function convertAudio(source, name, { maxFrames, maxCues }) {
  const bytes = source instanceof Uint8Array ? source : new Uint8Array(source);
  const meta = inspectWav(bytes);

  let audio;
  try {
    const context = new OfflineAudioContext(2, 1, DEVICE_FORMAT.sampleRate); // decoding resamples
    audio = await context.decodeAudioData(bytes.slice().buffer);
  } catch (e) {
    throw new Error(`Can't decode "${name}". Supported: WAV, MP3, FLAC, Ogg, AAC/M4A.`, { cause: e });
  }

  const frames = Math.min(audio.length, maxFrames);
  const left = audio.getChannelData(0).subarray(0, frames);
  const right = (audio.numberOfChannels > 1 ? audio.getChannelData(1) : audio.getChannelData(0)).subarray(0, frames);

  let cues = [];
  if (meta && meta.cues.length && meta.sampleRate) {
    const ratio = DEVICE_FORMAT.sampleRate / meta.sampleRate;
    cues = [...new Set(meta.cues.map(c => Math.round(c * ratio)))]
      .filter(c => c < frames)
      .sort((a, b) => a - b)
      .slice(0, maxCues);
  }

  return {
    bytes: encodeWav({ left, right, cues, name }),
    truncated: audio.length > maxFrames,
    cues: cues.length,
  };
}
