// Types for @synthuxacademy/spotykach-transfer.

/** The parts of a Web Serial `SerialPort` the link uses; a real SerialPort fits. */
export interface SerialPortLike {
  open(options: { baudRate: number; bufferSize?: number }): Promise<void>;
  close(): Promise<void>;
  readonly readable: ReadableStream<Uint8Array> | null;
  readonly writable: WritableStream<Uint8Array> | null;
  getInfo(): { usbVendorId?: number; usbProductId?: number };
}

export declare const USB_VENDOR_ID: number;
export declare const USB_PRODUCT_ID: number;
export declare const PROTO_MAJOR: number;
export declare const PROTO_MINOR: number;

export declare const CMD: Readonly<{
  hello: 0x01; ping: 0x02; list: 0x03;
  uploadBegin: 0x10; uploadData: 0x11; uploadCommit: 0x12; uploadAbort: 0x13;
  downloadOpen: 0x20; downloadRead: 0x21; downloadClose: 0x22;
  remove: 0x30; configRead: 0x40; configWrite: 0x41; fileRead: 0x42; fileWrite: 0x43; exit: 0x7F;
}>;

export declare const STATUS: Readonly<{
  ok: 0; badFrame: 1; unknownCmd: 2; badState: 3; badArg: 4; noCard: 5; io: 6;
  notFound: 7; noSpace: 8; tooLarge: 9; badOffset: 10; badCrc: 11; badFormat: 12;
}>;

/** English messages, index = status code. */
export declare const STATUS_TEXT: readonly string[];

/** Why the device rejected a frame, see `DeviceError.frameError`. */
export declare const FRAME_ERROR: Readonly<{ unknown: 0; crc: 1; stale: 2; overflow: 3; badLength: 4 }>;
/** DOWNLOAD_READ offsets must be multiples of this; `readSlot` ranges are aligned for you. */
export declare const SECTOR_SIZE: number;

export declare const SLOT_OCCUPIED: number;
export declare const SLOT_LOADABLE: number;
export declare const SLOT_PARSED: number;

export declare const DEVICE_FORMAT: Readonly<{ sampleRate: 48000; channels: 2; bitsPerSample: 32; audioFormat: 3 }>;

export declare class DeviceError extends Error {
  constructor(status: number, message?: string, reply?: Uint8Array);
  readonly status: number;
  /** The reply payload, status byte first, when the device sent one. */
  readonly reply: Uint8Array | null;
  /** For a frame the device rejected (status badFrame): one of FRAME_ERROR, otherwise null. */
  readonly frameError: number | null;
}
export declare class TimeoutError extends Error {
  /** What had arrived when the request gave up. */
  readonly detail: string;
}
export declare class NeedsAuthorizationError extends Error {}
export declare class MidiPortNotFoundError extends Error {}
export declare class ProtocolMismatchError extends Error {
  readonly major: number;
  readonly minor: number;
}

export declare function crc32(data: Uint8Array, crc?: number): number;

export interface DeviceInfo {
  protoMajor: number;
  protoMinor: number;
  tapes: number;
  slots: number;
  /** `BGPRTY` */
  tapeNames: string;
  maxChunk: number;
  /** Deck buffer length; longer audio is truncated on load. */
  maxFrames: number;
  maxCues: number;
  totalKiB: number;
  freeKiB: number;
  firmware: string;
}

export interface SlotEntry {
  /** 0–5, index into `tapeNames` */
  tape: number;
  /** 0–5 */
  slot: number;
  /** SLOT_OCCUPIED | SLOT_LOADABLE | SLOT_PARSED */
  flags: number;
  fileSize: number;
  /** 0 when the header didn't parse */
  frames: number;
  sampleRate: number;
  cues: number;
  channels: number;
  bits: number;
  /** INAM, empty when the file has none */
  name: string;
}

export interface TransferOptions {
  /** 0–1 */
  onProgress?: (fraction: number) => void;
  signal?: AbortSignal;
}

export type RangeReader = (offset: number, length: number, options?: TransferOptions) => Promise<Uint8Array>;

export declare class SkLink {
  static open(port: SerialPortLike): Promise<SkLink>;
  readonly port: SerialPortLike;
  /** Called once when the connection is lost; not on `close()` / `exit()`. */
  onclose: ((error: Error) => void) | null;
  closed: boolean;
  /** The last HELLO result. */
  info: DeviceInfo | null;

  /** One request; resolves to the reply payload (status byte first). */
  request(type: number, payload?: Uint8Array, timeout?: number, stallMs?: number): Promise<Uint8Array>;
  /** Runs `work` with no other operation in between. */
  exclusive<T>(work: () => Promise<T>): Promise<T>;
  startKeepAlive(): void;
  stopKeepAlive(): void;

  hello(): Promise<DeviceInfo>;
  list(): Promise<SlotEntry[]>;
  /** Requires `hello()` first. Checks the file with `checkUploadable` before sending. */
  upload(tape: number, slot: number, bytes: Uint8Array, options?: TransferOptions): Promise<void>;
  /** Requires `hello()` first. Ranges may start at any offset. */
  readSlot<T>(tape: number, slot: number, work: (size: number, read: RangeReader) => Promise<T>): Promise<T>;
  /** Requires `hello()` first. */
  download(tape: number, slot: number, options?: TransferOptions): Promise<Uint8Array>;
  remove(tape: number, slot: number): Promise<void>;
  /** Protocol 1.1 and up (firmware with settings support): readConfig / writeConfig work. */
  hasConfig(): boolean;
  /** The text of `SK/config.txt`, null when the card has none. */
  readConfig(): Promise<string | null>;
  /** Replaces `SK/config.txt` atomically. The device reads it on power-on. Requires `hello()` first. */
  writeConfig(text: string): Promise<void>;
  /** Protocol 1.2 and up (firmware with file support): readFile / writeFile work. */
  hasFiles(): boolean;
  /** A small text file (`isFileName`), null when the card has none. */
  readFile(name: string): Promise<string | null>;
  /** Creates or replaces a small text file (`isFileName`), at most `MAX_FILE_SIZE` bytes. Requires `hello()` first. */
  writeFile(name: string, text: string): Promise<void>;
  /** Back to USB MIDI, then closes the port. */
  exit(): Promise<void>;
  /** Closes the port without telling the device. */
  close(): Promise<void>;
}

export interface Connection {
  link: SkLink;
  info: DeviceInfo;
}

export declare function isSupported(): boolean;
export declare function grantedPort(): Promise<SerialPortLike | null>;
/** `onStep` gets a line for each connection step, for diagnostics. */
export interface ConnectOptions {
  onStep?: (line: string) => void;
}
export declare function enterTransferMode(options?: ConnectOptions): Promise<void>;
export declare function handshake(port: SerialPortLike, options?: ConnectOptions): Promise<Connection>;
/** Throws `NeedsAuthorizationError` on first use once the click expired; then call `authorizePort()` from a click. */
export declare function connect(options?: ConnectOptions): Promise<Connection>;
export declare function authorizePort(options?: ConnectOptions): Promise<Connection>;

export interface WavInfo {
  audioFormat: number;
  channels: number;
  sampleRate: number;
  bitsPerSample: number;
  blockAlign: number;
  /** -1 without a data chunk */
  dataOffset: number;
  dataSize: number;
  frames: number;
  /** dwSampleOffset of each cue point, in file order */
  cues: number[];
  /** LIST/INFO/INAM */
  name: string;
}

export declare function inspectWav(bytes: Uint8Array): WavInfo | null;
export declare function checkUploadable(bytes: Uint8Array, limits?: Pick<DeviceInfo, "maxFrames" | "maxCues">): string[];
export declare function encodeWav(audio: { left: Float32Array; right: Float32Array; cues?: number[]; name?: string }): Uint8Array;
export declare function convertAudio(
  source: ArrayBuffer | Uint8Array,
  name: string,
  limits: Pick<DeviceInfo, "maxFrames" | "maxCues">,
): Promise<{ bytes: Uint8Array; truncated: boolean; cues: number }>;

export type ConfigKey =
  | "mid_ch_a" | "mid_ch_b" | "mid_ps_a" | "mid_ps_b" | "pre_load"
  | "slc_mn_a" | "slc_mn_b" | "cue_sm_a" | "cue_sm_b" | "grt_mx_a" | "grt_mx_b";

export type ConfigValues = Record<ConfigKey, number>;

/** Keys the firmware reads from `SK/config.txt`, with their range and the value used when missing. */
export declare const CONFIG_KEYS: Readonly<Record<ConfigKey, Readonly<{ min: number; max: number; default: number }>>>;
/** `cue_sm_a` / `cue_sm_b`: ignore cue points, snap (Alt+Size free), free (Alt+Size snaps). */
export declare const CUE_SIZE_MODE: Readonly<{ ignore: 0; snap: 1; free: 2 }>;

/** Every key's value as the device reads the file; missing keys get their default, values are clamped. */
export declare function parseConfig(text: string | null): ConfigValues;
/** `text` with the values written in; unknown keys and other lines are kept, missing keys appended. */
export declare function formatConfig(values: Partial<ConfigValues>, text?: string | null): string;

/** The largest file `readFile` / `writeFile` handle (32 KB less the path field). */
export declare const MAX_FILE_SIZE: number;
/** `NAME.EXT` or `SK/NAME.EXT`, with a .txt, .md or .json extension: a file the device reads and writes. */
export declare function isFileName(name: string): boolean;
