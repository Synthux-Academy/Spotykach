/**
 * Spotykach USB transfer protocol, host side.
 *
 * Switches the device into transfer mode over USB MIDI (SysEx), talks to it over
 * Web Serial, and prepares WAV files the way the device loads them. Framework-free
 * ES module; the firmware side lives in src/transfer/ of the same repository, the
 * protocol is described in docs/usb-transfer.md.
 *
 * MIT License, Copyright (c) 2026 Synthux Academy
 */

// Protocol ///////////////////////////////////////////////////////////////////

export const USB_VENDOR_ID = 0x0483;
export const USB_PRODUCT_ID = 0x5740;

/** Protocol version this module speaks; the package major version follows the protocol major. */
export const PROTO_MAJOR = 1;
export const PROTO_MINOR = 0;

const SYSEX_ENTER = [0xF0, 0x7D, 0x53, 0x4B, 0x01, 0xF7];
const SYSEX_REPLY_PREFIX = [0xF0, 0x7D, 0x53, 0x4B, 0x02];

const MAGIC_0 = 0x53; // "S"
const MAGIC_1 = 0x4B; // "K"
const HEADER_SIZE = 12;
const MAX_PAYLOAD = 32768 + 16;
const RESPONSE_FLAG = 0x80;
const FRAME_ERROR_TYPE = 0x80; // response type of the reserved command 0x00

export const CMD = Object.freeze({
  hello: 0x01,
  ping: 0x02,
  list: 0x03,
  uploadBegin: 0x10,
  uploadData: 0x11,
  uploadCommit: 0x12,
  uploadAbort: 0x13,
  downloadOpen: 0x20,
  downloadRead: 0x21,
  downloadClose: 0x22,
  remove: 0x30,
  exit: 0x7F,
});

export const STATUS = Object.freeze({
  ok: 0,
  badFrame: 1,
  unknownCmd: 2,
  badState: 3,
  badArg: 4,
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

/** Why the device rejected a frame: `DeviceError.frameError` of a BAD_FRAME from the device */
export const FRAME_ERROR = Object.freeze({
  unknown: 0,
  crc: 1,       // checksum mismatch, bytes changed on the way
  stale: 2,     // a partial request stopped arriving
  overflow: 3,  // more data than the device's receive buffer holds
  badLength: 4, // the header announced a payload that's too large
});

const FRAME_ERROR_TEXT = ["", "checksum mismatch", "the request stopped arriving", "receive buffer overflow", "invalid length"];

/** DOWNLOAD_READ offsets must be multiples of this; `readSlot` ranges are aligned for you. */
export const SECTOR_SIZE = 512;

/** LIST entry flags */
export const SLOT_OCCUPIED = 1 << 0;
export const SLOT_LOADABLE = 1 << 1;
export const SLOT_PARSED = 1 << 2;

/** The only format the device loads */
export const DEVICE_FORMAT = Object.freeze({ sampleRate: 48000, channels: 2, bitsPerSample: 32, audioFormat: 3 });

// Errors /////////////////////////////////////////////////////////////////////

/** The device answered with a status other than OK. `reply` is the reply payload when there was one. */
export class DeviceError extends Error {
  constructor(status, message, reply) {
    super(message || STATUS_TEXT[status] || `Spotykach error ${status}.`);
    this.name = "DeviceError";
    this.status = status;
    this.reply = reply || null;
    /** For a frame the device rejected: one of FRAME_ERROR, otherwise null. */
    this.frameError = null;
  }
}

/** No (complete) reply in time. `detail` tells a silent device apart from bytes lost on the way. */
export class TimeoutError extends Error {
  constructor(detail) {
    super("Spotykach didn't respond in time.");
    this.name = "TimeoutError";
    this.detail = detail;
  }
}

/** First use on this computer: the serial port needs a grant, call `authorizePort()` from a click. */
export class NeedsAuthorizationError extends Error {
  constructor() {
    super("Select Spotykach in the browser's port picker. This is needed only once per computer.");
    this.name = "NeedsAuthorizationError";
  }
}

/** No Spotykach USB MIDI port: not connected, or already in transfer mode without a granted serial port. */
export class MidiPortNotFoundError extends Error {
  constructor() {
    super("Spotykach MIDI port not found. Is it connected through the rear USB-C port?" + portHint());
    this.name = "MidiPortNotFoundError";
  }
}

/** The device speaks another protocol major version. */
export class ProtocolMismatchError extends Error {
  constructor(major, minor) {
    super(`Spotykach uses transfer protocol ${major}.${minor}, this app supports ${PROTO_MAJOR}.x. Update the app or the firmware.`);
    this.name = "ProtocolMismatchError";
    this.major = major;
    this.minor = minor;
  }
}

// Helpers ////////////////////////////////////////////////////////////////////

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const now = () => (typeof performance !== "undefined" ? performance.now() : Date.now());
const platform = () => (typeof navigator !== "undefined" && navigator.platform) || "";
const isWindows = () => /Win/.test(platform());
const isLinux = () => /Linux/.test(platform()) && !/Android/.test((typeof navigator !== "undefined" && navigator.userAgent) || "");
const portHint = () => (isWindows() ? " On Windows, close other apps (e.g. your DAW) that use its MIDI port." : "");
const slotArgs = (tape, slot) => new Uint8Array([tape, slot, 0, 0]);

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
    table[i] = c >>> 0;
  }
  return table;
})();

/** CRC-32 (IEEE 802.3, same as zlib). Pass the previous result as `crc` to continue over more data. */
export function crc32(data, crc = 0) {
  crc = ~crc >>> 0;
  for (let i = 0; i < data.length; i++) crc = CRC_TABLE[(crc ^ data[i]) & 0xff] ^ (crc >>> 8);
  return ~crc >>> 0;
}

/*
Downloads: DOWNLOAD_READ names an absolute offset and length, so it can be repeated.
On macOS the serial tty can drop the tail of a large burst when the browser doesn't
drain it fast enough, so the read size adapts: halve on a lost reply and stay below
the size that failed; try that size again only after a long run of clean reads.
*/
const MIN_READ_CHUNK = 1024;
const GROW_AFTER = 8;
const PROBE_AFTER = 64;
const MAX_READ_FAILURES_IN_ROW = 8;
const READ_CHUNK_KEY = "spotykach.readChunk";

/*
Uploads: a chunk is resent after a corrupted frame or a lost reply. The device only
advances after writing a chunk and reports its offset with every UPLOAD_DATA reply,
so BAD_OFFSET on a resend tells whether the chunk was written already.
*/
const MAX_UPLOAD_RETRIES = 5;
const UPLOAD_DATA_TIMEOUT_MS = 4000; // well below the device's 10 s idle timeout
const DEVICE_QUIET_MS = 250;         // the device drops a broken frame's rest until 100 ms of quiet

const KEEP_ALIVE_MS = 2000;

function loadReadChunk(maxChunk) {
  try {
    const saved = parseInt(globalThis.window?.localStorage?.getItem(READ_CHUNK_KEY) ?? "", 10);
    if (saved >= MIN_READ_CHUNK) return Math.min(saved, maxChunk);
  } catch (e) { /* storage blocked */ }
  return maxChunk;
}

function saveReadChunk(chunk) {
  try {
    globalThis.window?.localStorage?.setItem(READ_CHUNK_KEY, String(chunk));
  } catch (e) { /* storage blocked */ }
}

/**
 * `e` with `detail` appended to its message. The serial port's own errors are
 * DOMExceptions, whose message can't be set: those come back wrapped, as `cause`.
 */
function withDetail(e, detail) {
  try {
    e.message += ` (${detail})`;
    return e;
  } catch (_) {
    return new Error(`${e.message} (${detail})`, { cause: e });
  }
}

const isRetriable = e => e instanceof TimeoutError || (e instanceof DeviceError && e.status === STATUS.badFrame);

// Link ///////////////////////////////////////////////////////////////////////

/**
 * One open serial connection to a Spotykach in transfer mode.
 *
 * Requests run strictly one at a time. Operations made of several requests
 * (an upload, a download, HELLO which resets the device session) also run one
 * at a time, through `exclusive()`.
 */
export class SkLink {
  /** Use `SkLink.open(port)`. */
  constructor(port) {
    this.port = port;
    /** Called once when the connection is lost; not on `close()` / `exit()`. */
    this.onclose = null;
    this.closed = false;
    /** The last HELLO result. */
    this.info = null;

    this._rx = new Uint8Array(0);
    this._lastRxMs = 0;
    this._lastTxMs = 0;
    this._seq = 0;
    this._pending = null;
    this._requestLock = Promise.resolve();
    this._opLock = Promise.resolve();
    this._ops = 0;
    this._reader = null;
    this._writer = null;
    this._keepAlive = null;
    this._onSerialDisconnect = e => {
      if (e.target === this.port) this._shutdown(new Error("Spotykach disconnected."));
    };
  }

  /** Opens the port, retrying while the device is still enumerating. Doesn't talk to it yet. */
  static async open(port) {
    const link = new SkLink(port);
    for (let attempt = 0; ; attempt++) {
      try {
        await port.open({ baudRate: 115200, bufferSize: 65536 });
        break;
      } catch (e) {
        if (attempt >= 7) {
          throw new Error("Can't open the Spotykach serial port. Is another tab or app using it?"
            + (isLinux() ? " On Linux, add your user to the 'dialout' group (or 'uucp' on Arch) and log in again." : ""),
          { cause: e });
        }
        await sleep(250);
      }
    }
    link._writer = port.writable.getWriter();
    link._reader = port.readable.getReader();
    globalThis.navigator?.serial?.addEventListener?.("disconnect", link._onSerialDisconnect);
    link._readLoop();
    return link;
  }

  // Frames ///////////////////////////////////////////////////////////////////

  async _readLoop() {
    try {
      while (this._reader) {
        const { value, done } = await this._reader.read();
        if (done) break;
        if (value && value.length) this._append(value);
      }
    } catch (e) { /* device went away */ }
    this._shutdown(new Error("Connection to Spotykach lost."));
  }

  _append(bytes) {
    this._lastRxMs = now();
    const merged = new Uint8Array(this._rx.length + bytes.length);
    merged.set(this._rx);
    merged.set(bytes, this._rx.length);
    this._rx = merged;
    while (this._rx.length >= HEADER_SIZE) {
      // Bytes before a magic are dropped, which also resyncs after garbage
      if (this._rx[0] !== MAGIC_0 || this._rx[1] !== MAGIC_1) { this._rx = this._rx.subarray(1); continue; }
      const dv = new DataView(this._rx.buffer, this._rx.byteOffset);
      const length = dv.getUint32(4, true);
      if (length > MAX_PAYLOAD) { this._rx = this._rx.subarray(1); continue; }
      if (this._rx.length < HEADER_SIZE + length) break;
      const frame = this._rx.slice(0, HEADER_SIZE + length);
      this._rx = this._rx.slice(HEADER_SIZE + length);
      const crc = crc32(frame.subarray(HEADER_SIZE), crc32(frame.subarray(2, 8)));
      if (crc !== dv.getUint32(8, true)) {
        this._fail(new DeviceError(STATUS.badFrame, "A reply from Spotykach arrived corrupted. Please retry."));
        continue;
      }
      this._deliver(frame[2], frame[3], frame.subarray(HEADER_SIZE));
    }
  }

  _deliver(type, seq, payload) {
    const pending = this._pending;
    if (!pending) return;
    if (type === FRAME_ERROR_TYPE) {
      const reason = payload[1] ?? FRAME_ERROR.unknown;
      const text = FRAME_ERROR_TEXT[reason];
      const error = new DeviceError(payload[0] ?? STATUS.badFrame,
        `Spotykach received a corrupted request${text ? ` (${text})` : ""}. Please retry.`, payload);
      error.frameError = reason;
      this._fail(error);
      return;
    }
    if (type !== (pending.type | RESPONSE_FLAG) || seq !== pending.seq) return; // a late reply to an abandoned request
    this._pending = null;
    clearInterval(pending.timer);
    pending.resolve(payload);
  }

  /** What had arrived when a request timed out. */
  _rxState() {
    const rx = this._rx;
    if (rx.length < HEADER_SIZE) return `${rx.length} bytes pending`;
    const dv = new DataView(rx.buffer, rx.byteOffset);
    const aligned = rx[0] === MAGIC_0 && rx[1] === MAGIC_1;
    return `${rx.length} of ${HEADER_SIZE + dv.getUint32(4, true)} bytes of a ${aligned ? "" : "misaligned "}frame type 0x${rx[2].toString(16)} seq ${rx[3]} pending`;
  }

  /** Drops partially received data before a retry. */
  _resync() {
    this._rx = new Uint8Array(0);
  }

  _fail(error) {
    const pending = this._pending;
    if (!pending) return;
    this._pending = null;
    clearInterval(pending.timer);
    pending.reject(error);
  }

  _shutdown(error) {
    if (this.closed) return;
    this.closed = true;
    this.stopKeepAlive();
    globalThis.navigator?.serial?.removeEventListener?.("disconnect", this._onSerialDisconnect);
    this._fail(error);
    if (this.onclose) this.onclose(error);
  }

  /** Closes the port without telling the device; `exit()` does both. */
  async close() {
    this.closed = true;
    this.stopKeepAlive();
    globalThis.navigator?.serial?.removeEventListener?.("disconnect", this._onSerialDisconnect);
    this._fail(new Error("Disconnected."));
    const reader = this._reader;
    this._reader = null;
    try { await reader?.cancel(); } catch (e) { /* gone */ }
    try { reader?.releaseLock(); } catch (e) { /* gone */ }
    try { this._writer?.releaseLock(); } catch (e) { /* gone */ }
    try { await this.port.close(); } catch (e) { /* gone */ }
  }

  /**
   * Sends one request and returns the reply payload (status byte included).
   * Throws `DeviceError` for a status other than OK, `TimeoutError` without a reply.
   * `stallMs`: give up early once a reply started arriving and then stopped.
   */
  request(type, payload = new Uint8Array(0), timeout = 5000, stallMs = 0) {
    const run = () => this._send(type, payload, timeout, stallMs);
    const result = this._requestLock.then(run, run);
    this._requestLock = result.catch(() => {});
    return result;
  }

  async _send(type, payload, timeout, stallMs) {
    if (this.closed || !this._writer) throw new Error("Not connected to Spotykach.");
    this._seq = (this._seq + 1) & 0xff;
    const seq = this._seq;
    const frame = new Uint8Array(HEADER_SIZE + payload.length);
    const dv = new DataView(frame.buffer);
    frame[0] = MAGIC_0;
    frame[1] = MAGIC_1;
    frame[2] = type;
    frame[3] = seq;
    dv.setUint32(4, payload.length, true);
    frame.set(payload, HEADER_SIZE);
    dv.setUint32(8, crc32(payload, crc32(frame.subarray(2, 8))), true);

    const response = new Promise((resolve, reject) => {
      const start = now();
      const timer = setInterval(() => {
        const t = now();
        const stalled = stallMs > 0 && this._rx.length > 0 && t - this._lastRxMs > stallMs;
        if (stalled || t - start > timeout) {
          this._fail(new TimeoutError(`request 0x${type.toString(16)} seq ${seq}: ${this._rxState()}`));
        }
      }, 50);
      this._pending = { type, seq, resolve, reject, timer };
    });
    // The device can reject a frame before the write finishes; it's awaited below
    response.catch(() => {});
    this._lastTxMs = now();
    await this._writer.write(frame);
    const reply = await response;
    if (reply[0] !== STATUS.ok) throw new DeviceError(reply[0], undefined, reply);
    return reply;
  }

  /** Runs `work` with no other operation in between, e.g. a whole upload. */
  exclusive(work) {
    const run = async () => {
      this._ops++;
      try {
        return await work();
      } finally {
        this._ops--;
      }
    };
    const result = this._opLock.then(run, run);
    this._opLock = result.catch(() => {});
    return result;
  }

  /** PINGs while nothing else talks: the device leaves transfer mode after 10 s without a frame. */
  startKeepAlive() {
    this.stopKeepAlive();
    this._keepAlive = setInterval(() => {
      if (this.closed || this._ops > 0 || this._pending) return;
      if (now() - this._lastTxMs < KEEP_ALIVE_MS) return;
      this.request(CMD.ping).catch(() => {});
    }, 500);
  }

  stopKeepAlive() {
    if (this._keepAlive) clearInterval(this._keepAlive);
    this._keepAlive = null;
  }

  // Commands /////////////////////////////////////////////////////////////////

  /** Device and card info. Also resets the device session, so it never runs inside another operation. */
  async hello() {
    const p = await this.exclusive(() => this.request(CMD.hello));
    const dv = new DataView(p.buffer, p.byteOffset, p.byteLength);
    const decoder = new TextDecoder();
    this.info = {
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
    return this.info;
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

  async _sendChunk(payload, offset, length) {
    for (let attempt = 1; ; attempt++) {
      try {
        await this.request(CMD.uploadData, payload, UPLOAD_DATA_TIMEOUT_MS);
        return;
      } catch (e) {
        const reply = e instanceof DeviceError ? e.reply : null;
        const deviceOffset = reply && reply.length >= 8 ? new DataView(reply.buffer, reply.byteOffset).getUint32(4, true) : -1;
        const isResendOffset = attempt > 1 && e instanceof DeviceError && e.status === STATUS.badOffset;
        if (isResendOffset && deviceOffset === offset + length) return; // written, only the reply was lost
        const retriable = isRetriable(e) || (isResendOffset && deviceOffset === offset);
        const detail = `upload at byte ${offset}, attempt ${attempt}: ${e.detail || e.message}`;
        console.warn(`[spotykach] ${detail}`);
        if (!retriable || this.closed || attempt > MAX_UPLOAD_RETRIES) {
          throw withDetail(e, detail);
        }
        await sleep(DEVICE_QUIET_MS);
        this._resync();
      }
    }
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

  async _readRange(start, length, maxChunk, options = {}) {
    const { onProgress, signal } = options;
    const out = new Uint8Array(length);
    let chunk = loadReadChunk(maxChunk);
    let ceiling = chunk; // largest read size not known to fail
    let clean = 0;
    let failures = 0;
    let failuresInRow = 0;
    for (let done = 0; done < length;) {
      signal?.throwIfAborted();
      const size = Math.min(chunk, length - done);
      const args = new Uint8Array(8);
      const dv = new DataView(args.buffer);
      dv.setUint32(0, start + done, true);
      dv.setUint32(4, size, true);
      let p;
      try {
        p = await this.request(CMD.downloadRead, args, 1500, 300);
      } catch (e) {
        const detail = `read at byte ${start + done}, ${size} bytes: ${e.detail || e.message}`;
        console.warn(`[spotykach] ${detail}`);
        failures++;
        if (!isRetriable(e) || this.closed || ++failuresInRow > MAX_READ_FAILURES_IN_ROW) {
          throw withDetail(e, detail);
        }
        this._resync();
        ceiling = Math.max(MIN_READ_CHUNK, chunk >> 1);
        chunk = ceiling;
        clean = 0;
        continue;
      }
      out.set(p.subarray(4, 4 + size), done);
      done += size;
      onProgress?.(done / length);
      failuresInRow = 0;
      clean++;
      if (chunk < ceiling && clean >= GROW_AFTER) {
        chunk = Math.min(ceiling, chunk << 1);
        clean = 0;
      }
      else if (chunk === ceiling && ceiling < maxChunk && clean >= PROBE_AFTER) {
        ceiling = Math.min(maxChunk, ceiling << 1);
        chunk = ceiling;
        clean = 0;
      }
    }
    // Small reads (e.g. headers) say nothing about the size that works
    if (length >= maxChunk) saveReadChunk(ceiling);
    if (failures) console.info(`[spotykach] ${failures} lost replies, finished with ${chunk} byte reads`);
    return out;
  }

  async remove(tape, slot) {
    await this.exclusive(() => this.request(CMD.remove, slotArgs(tape, slot)));
  }

  /** Back to USB MIDI and closes the port. Best effort: the device also leaves after 10 s of silence. */
  async exit() {
    this.stopKeepAlive();
    if (!this.closed) {
      try { await this.request(CMD.exit, undefined, 1000); } catch (e) { /* the timeout takes over */ }
    }
    this.onclose = null;
    await this.close();
  }
}

// Connecting /////////////////////////////////////////////////////////////////

/** Web Serial and Web MIDI are both needed (Chromium-based browsers on desktop). */
export function isSupported() {
  return typeof navigator !== "undefined" && "serial" in navigator && "requestMIDIAccess" in navigator;
}

/** The serial port this browser already has permission for, present while the device is in transfer mode. */
export async function grantedPort() {
  const ports = (await globalThis.navigator?.serial?.getPorts()) ?? [];
  return ports.find(port => {
    const info = port.getInfo();
    return info.usbVendorId === USB_VENDOR_ID && info.usbProductId === USB_PRODUCT_ID;
  }) ?? null;
}

async function waitForPort(timeoutMs) {
  const start = now();
  while (now() - start < timeoutMs) {
    const port = await grantedPort();
    if (port) return port;
    await sleep(250);
  }
  return null;
}

const ENTER_TEXT = {
  1: "Spotykach is busy (loading, saving or recording). Finish that and try again.",
  2: "No SD card found in Spotykach.",
};

// Web MIDI's calls have no time limit of their own; on some Windows machines they never settle
const MIDI_ACCESS_MS = 8000;
const MIDI_OPEN_MS = 5000;

function within(promise, ms, message) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(message)), ms);
    promise.then(resolve, reject).finally(() => clearTimeout(timer));
  });
}

async function midiPermission() {
  try {
    return (await navigator.permissions.query({ name: "midi", sysex: true })).state;
  } catch (_) {
    return "unknown";
  }
}

/**
 * Asks the device over USB MIDI to switch to transfer mode and waits for its reply.
 * Both MIDI ports are opened before sending so the reply can't be missed; the
 * request is sent twice at most. A MIDI port that disappears meanwhile means the
 * device switched and only its reply got lost.
 */
export async function enterTransferMode() {
  const permission = await midiPermission();
  const request = navigator.requestMIDIAccess({ sysex: true }).catch(e => {
    throw new Error("MIDI access was denied. Spotykach is switched to transfer mode through MIDI SysEx, please allow it.", { cause: e });
  });
  // A permission prompt waits on the user, so no time limit then
  const access = permission === "prompt" ? await request : await within(request, MIDI_ACCESS_MS,
    `The browser didn't give access to MIDI within ${MIDI_ACCESS_MS / 1000} s. Close other apps and tabs that use MIDI, or try another Chromium-based browser.`);
  const isSpotykach = port => /spotykach/i.test(port.name || "");
  const output = [...access.outputs.values()].find(isSpotykach);
  const inputs = [...access.inputs.values()].filter(isSpotykach);
  if (!output) throw new MidiPortNotFoundError();

  try {
    await within(Promise.all([output.open(), ...inputs.map(input => input.open())]), MIDI_OPEN_MS,
      `The MIDI port didn't open within ${MIDI_OPEN_MS / 1000} s.`);
  } catch (e) {
    throw new Error("Can't open the Spotykach MIDI port." + portHint(), { cause: e });
  }

  let status = null;
  let onReply = () => {};
  const isReply = d => d && d.length === 7 && SYSEX_REPLY_PREFIX.every((b, i) => d[i] === b) && d[6] === 0xF7;
  for (const input of inputs) {
    input.onmidimessage = e => {
      if (!isReply(e.data)) return;
      status = e.data[5];
      onReply();
    };
  }
  const switched = () => output.state === "disconnected";

  try {
    for (let attempt = 0; attempt < 2 && status === null; attempt++) {
      if (switched()) return;
      try {
        output.send(SYSEX_ENTER);
      } catch (e) {
        if (switched()) return;
        throw new Error("Can't send to the Spotykach MIDI port." + portHint(), { cause: e });
      }
      // Mounting the card can take ~600 ms before the device replies
      await new Promise(resolve => {
        const timer = setTimeout(resolve, attempt === 0 ? 2000 : 3000);
        onReply = () => { clearTimeout(timer); resolve(); };
      });
    }
  } finally {
    for (const input of inputs) input.onmidimessage = null;
  }

  if (status === null) {
    if (switched()) return;
    throw new Error("Spotykach didn't respond. Its firmware may not support USB transfer." + portHint());
  }
  if (status !== 0) throw new Error(ENTER_TEXT[status] || `Spotykach refused transfer mode (${status}).`);
}

/** Opens the port and checks HELLO. Resolves to `{ link, info }`, with the keep-alive running. */
export async function handshake(port) {
  const link = await SkLink.open(port);
  try {
    const info = await link.hello();
    if (info.protoMajor !== PROTO_MAJOR) {
      await link.exit();
      throw new ProtocolMismatchError(info.protoMajor, info.protoMinor);
    }
    link.startKeepAlive();
    return { link, info };
  } catch (e) {
    await link.close();
    throw e;
  }
}

/**
 * Connects to a Spotykach: reuses a port that's already in transfer mode (e.g. after
 * a reload), otherwise switches the device and waits for its serial port.
 * On first use, once the click that started this has expired, throws
 * `NeedsAuthorizationError`: call `authorizePort()` from a fresh click.
 */
export async function connect() {
  if (!isSupported()) {
    throw new Error("USB transfer needs Web Serial and Web MIDI. Please use a Chromium-based browser (Chrome, Edge, Opera) on macOS, Windows or Linux.");
  }

  const existing = await grantedPort();
  if (existing) {
    try {
      return await handshake(existing);
    } catch (e) {
      if (e instanceof ProtocolMismatchError) throw e;
      // A stale grant, the device isn't in transfer mode: switch it
    }
  }

  try {
    await enterTransferMode();
  } catch (e) {
    // Maybe already in transfer mode with a port this browser hasn't been granted yet
    if (e instanceof MidiPortNotFoundError && navigator.userActivation?.isActive) {
      try { return await authorizePort(); } catch (_) { throw e; }
    }
    throw e;
  }

  const port = await waitForPort(2500);
  if (port) return handshake(port);
  // The click that started this still counts as a user gesture for about 5 s
  if (navigator.userActivation?.isActive) return authorizePort();
  throw new NeedsAuthorizationError();
}

/** Shows the browser's port picker, so it must run from a click. The device waits up to 60 s for it. */
export async function authorizePort() {
  const port = await navigator.serial.requestPort({ filters: [{ usbVendorId: USB_VENDOR_ID, usbProductId: USB_PRODUCT_ID }] });
  return handshake(port);
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
