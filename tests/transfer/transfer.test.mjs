// @synthux/spotykach-transfer against the firmware's transfer code (build/harness),
// over a fake Web Serial port. Run through run.sh.
import { test } from "node:test";
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import {
  SkLink, CMD, STATUS, FRAME_ERROR, DeviceError, SLOT_OCCUPIED, SLOT_LOADABLE, SLOT_PARSED,
  crc32, encodeWav, inspectWav, checkUploadable,
} from "../../web/spotykach-transfer/index.js";

const HARNESS = process.env.HARNESS;
assert.ok(HARNESS, "HARNESS must point to the built harness, use run.sh");

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

/*
Starts the device harness on an empty card and connects a link to it.
fault: harness-side fault (see harness.cpp); onWrite(bytes, write): host-side hook
that may alter or delay a frame on its way to the device.
*/
async function startDevice({ fault, onWrite } = {}) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "sk-card-"));
  const env = { ...process.env };
  if (fault) env.FAULT = fault;
  const child = spawn(HARNESS, [root], { stdio: ["pipe", "pipe", "pipe"], env });
  let stderr = "";
  child.stderr.on("data", d => { stderr += d; });
  const exited = new Promise(resolve => child.on("exit", code => resolve(code)));

  const queue = [];
  let wake = null;
  let ended = false;
  child.stdout.on("data", d => { queue.push(new Uint8Array(d)); wake?.(); });
  child.stdout.on("end", () => { ended = true; wake?.(); });

  let writes = Promise.resolve();
  const write = bytes => child.stdin.write(Buffer.from(bytes));
  const port = {
    async open() {},
    async close() {},
    readable: {
      getReader: () => ({
        async read() {
          while (!queue.length && !ended) await new Promise(resolve => { wake = resolve; });
          return queue.length ? { value: queue.shift(), done: false } : { done: true };
        },
        async cancel() {},
        releaseLock() {},
      }),
    },
    writable: {
      getWriter: () => ({
        // Web Serial keeps writes in order
        write(bytes) {
          writes = writes.then(() => (onWrite ? onWrite(bytes, write) : write(bytes)));
          return writes;
        },
        releaseLock() {},
      }),
    },
  };

  const link = await SkLink.open(port);
  const info = await link.hello();
  return {
    link, info, root,
    async stop() {
      if (!link.closed) await link.exit();
      const code = await exited;
      fs.rmSync(root, { recursive: true, force: true });
      assert.equal(code, 0, `harness exited with ${code}: ${stderr}`);
    },
  };
}

function testAudio(seconds, seed = 1) {
  const frames = Math.round(seconds * 48000);
  const left = new Float32Array(frames);
  const right = new Float32Array(frames);
  for (let i = 0; i < frames; i++) {
    left[i] = Math.sin((i + seed) / 7);
    right[i] = Math.cos((i * seed) / 11);
  }
  return { left, right, frames };
}

// Every 5th data frame of a given type
function nthFrame(type, n, action) {
  let count = 0;
  return (bytes, write) => (bytes[2] === type && ++count === n ? action(bytes, write) : write(bytes));
}

test("hello, list, upload, download, delete", async () => {
  const device = await startDevice();
  const { link, info } = device;
  assert.equal(info.protoMajor, 1);
  assert.equal(info.tapeNames, "BGPRTY");
  assert.equal(info.maxFrames, 42 * 48000);
  assert.equal(info.maxCues, 255);

  let slots = await link.list();
  assert.equal(slots.length, 36);
  assert.ok(slots.every(s => !(s.flags & SLOT_OCCUPIED)));

  const { left, right, frames } = testAudio(3);
  const cues = Array.from({ length: 255 }, (_, i) => i * 500);
  const name = "Kick — ünïcode.wav";
  const wav = encodeWav({ left, right, cues, name });
  let progress = 0;
  await link.upload(1, 2, wav, { onProgress: p => { progress = p; } });
  assert.equal(progress, 1);

  slots = await link.list();
  const entry = slots.find(s => s.tape === 1 && s.slot === 2);
  assert.equal(entry.flags, SLOT_OCCUPIED | SLOT_LOADABLE | SLOT_PARSED);
  assert.equal(entry.frames, frames);
  assert.equal(entry.cues, 255);
  assert.equal(entry.name, name);
  assert.ok(fs.existsSync(path.join(device.root, "SK/G/3.wav")));
  assert.ok(!fs.existsSync(path.join(device.root, "SK/G/3.tmp")));

  const back = await link.download(1, 2);
  assert.deepEqual(back, wav);

  await link.remove(1, 2);
  slots = await link.list();
  assert.ok(!(slots.find(s => s.tape === 1 && s.slot === 2).flags & SLOT_OCCUPIED));
  await device.stop();
});

test("readSlot reads ranges at any offset, e.g. the cue chunk after the audio, the device only aligned ones", async () => {
  const device = await startDevice();
  const { link } = device;
  const { left, right } = testAudio(2);
  const wav = encodeWav({ left, right, cues: [0, 1000, 50000], name: "kick.wav" });
  await link.upload(0, 0, wav);

  const local = inspectWav(wav);
  const dataEnd = local.dataOffset + local.dataSize;
  assert.notEqual(dataEnd % 512, 0, "the test needs an unaligned tail");
  const { head, tail, odd } = await link.readSlot(0, 0, async (size, read) => {
    const args = new Uint8Array(8);
    new DataView(args.buffer).setUint32(0, 1234, true);
    new DataView(args.buffer).setUint32(4, 100, true);
    await assert.rejects(link.request(CMD.downloadRead, args), e => e.status === STATUS.badArg);
    return {
      head: await read(0, 4096),
      tail: await read(dataEnd, size - dataEnd),
      odd: await read(1234, 40000), // unaligned and longer than one chunk
    };
  });
  assert.deepEqual(head, wav.subarray(0, 4096));
  assert.deepEqual(tail, wav.subarray(dataEnd));
  assert.deepEqual(odd, wav.subarray(1234, 1234 + 40000));
  await device.stop();
});

for (const [name, options] of [
  ["a corrupted request", { onWrite: nthFrame(CMD.uploadData, 5, (b, write) => { const c = b.slice(); c[5000] ^= 0x55; return write(c); }) }],
  ["a lost reply", { fault: "upload-noreply" }],
  ["a request that stalls for 2.5 s", { onWrite: nthFrame(CMD.uploadData, 5, async (b, write) => { write(b.subarray(0, 10000)); await sleep(2500); write(b.subarray(10000)); }) }],
]) {
  test(`upload recovers from ${name}`, async () => {
    const device = await startDevice(options);
    const { left, right } = testAudio(6);
    const wav = encodeWav({ left, right, cues: [0, 1000], name: "atmo.wav" });
    await device.link.upload(2, 1, wav);
    assert.deepEqual(new Uint8Array(fs.readFileSync(path.join(device.root, "SK/P/2.wav"))), wav);
    await device.stop();
  });
}

for (const fault of ["read-cut", "read-drop", "read-burst"]) {
  test(`download recovers from ${fault}`, async () => {
    const device = await startDevice({ fault });
    const { left, right } = testAudio(10, 3);
    const wav = encodeWav({ left, right, name: "long.wav" });
    await device.link.upload(5, 5, wav);
    assert.deepEqual(await device.link.download(5, 5), wav);
    await device.stop();
  });
}

test("failed uploads keep the old slot", async () => {
  const device = await startDevice();
  const { link, root } = device;
  const { left, right } = testAudio(1);
  const wav = encodeWav({ left, right, name: "keep.wav" });
  await link.upload(1, 2, wav);

  // Wrong whole-file CRC
  const begin = new Uint8Array(12);
  const dv = new DataView(begin.buffer);
  begin.set([1, 2, 0, 0]);
  dv.setUint32(4, wav.length, true);
  dv.setUint32(8, crc32(wav) ^ 1, true);
  await link.request(CMD.uploadBegin, begin);
  for (let o = 0; o < wav.length; o += 32768) {
    const chunk = wav.subarray(o, o + 32768);
    const payload = new Uint8Array(4 + chunk.length);
    new DataView(payload.buffer).setUint32(0, o, true);
    payload.set(chunk, 4);
    await link.request(CMD.uploadData, payload);
  }
  await assert.rejects(link.request(CMD.uploadCommit), e => e instanceof DeviceError && e.status === STATUS.badCrc);
  assert.deepEqual(new Uint8Array(fs.readFileSync(path.join(root, "SK/G/3.wav"))), wav);
  assert.ok(!fs.existsSync(path.join(root, "SK/G/3.tmp")));

  // Not a WAV: refused locally, and by the device when sent anyway
  const junk = new Uint8Array(1000).fill(7);
  await assert.rejects(link.upload(0, 0, junk), /isn't a WAV/);
  const junkBegin = new Uint8Array(12);
  new DataView(junkBegin.buffer).setUint32(4, junk.length, true);
  new DataView(junkBegin.buffer).setUint32(8, crc32(junk), true);
  await link.request(CMD.uploadBegin, junkBegin);
  const junkData = new Uint8Array(4 + junk.length);
  junkData.set(junk, 4);
  await link.request(CMD.uploadData, junkData);
  await assert.rejects(link.request(CMD.uploadCommit), e => e.status === STATUS.badFormat);
  assert.ok(!fs.existsSync(path.join(root, "SK/B/1.wav")));
  await device.stop();
});

test("invalid requests", async () => {
  const device = await startDevice();
  const { link } = device;
  const status = s => e => e instanceof DeviceError && e.status === s;
  await assert.rejects(link.request(CMD.downloadRead, new Uint8Array(8)), status(STATUS.badState));
  await assert.rejects(link.request(CMD.remove, new Uint8Array([6, 0, 0, 0])), status(STATUS.badArg));
  await assert.rejects(link.request(0x55), status(STATUS.unknownCmd));
  await assert.rejects(link.download(0, 5), status(STATUS.notFound));

  // Upload chunks other than the last must keep the file position sector-aligned
  const begin = new Uint8Array(12);
  new DataView(begin.buffer).setUint32(4, 2000, true);
  await link.request(CMD.uploadBegin, begin);
  await assert.rejects(link.request(CMD.uploadData, new Uint8Array(4 + 1000)), status(STATUS.badArg));
  const ahead = new Uint8Array(4 + 512);
  new DataView(ahead.buffer).setUint32(0, 512, true);
  await assert.rejects(link.request(CMD.uploadData, ahead), status(STATUS.badOffset));
  await link.request(CMD.uploadAbort);
  await device.stop();
});

test("the device reports why it rejected a frame", async () => {
  let mode = null;
  const device = await startDevice({
    onWrite: async (bytes, write) => {
      if (bytes[2] !== CMD.ping || !mode) return write(bytes);
      const current = mode;
      mode = null;
      if (current === "crc") { const c = bytes.slice(); c[8] ^= 1; return write(c); }
      write(bytes.subarray(0, 6)); // header cut short, then silence
      await sleep(2500);
    },
  });
  mode = "crc";
  await assert.rejects(device.link.request(CMD.ping), e => e.status === STATUS.badFrame && e.frameError === FRAME_ERROR.crc && /checksum/.test(e.message));
  await sleep(250);
  mode = "stale";
  await assert.rejects(device.link.request(CMD.ping, undefined, 5000), e => e.frameError === FRAME_ERROR.stale && /stopped arriving/.test(e.message));
  await sleep(250);
  await device.link.request(CMD.ping);
  await device.stop();
});

test("garbage on the line is skipped", async () => {
  let first = true;
  const device = await startDevice({
    onWrite: (bytes, write) => {
      if (first && bytes[2] === CMD.ping) { first = false; write(new TextEncoder().encode("AT\r\nATZ\r\n")); }
      return write(bytes);
    },
  });
  await device.link.request(CMD.ping);
  await device.stop();
});

test("encodeWav: audio 4-byte aligned for every name length, inspectWav reads it back", () => {
  const left = new Float32Array([0, 0.5, -0.5, 1]);
  const right = new Float32Array([1, -1, 0.25, 0]);
  for (let n = 0; n <= 70; n++) {
    const name = "a".repeat(n);
    const wav = encodeWav({ left, right, cues: [1, 3], name });
    const info = inspectWav(wav);
    assert.equal(info.dataOffset % 4, 0, `name length ${n}`);
    assert.equal(new DataView(wav.buffer).getUint32(4, true), wav.length - 8);
    assert.equal(info.name, name.slice(0, 64));
    assert.deepEqual(info.cues, [1, 3]);
    assert.equal(info.frames, 4);
    assert.deepEqual(checkUploadable(wav, { maxFrames: 4, maxCues: 2 }), []);
  }
  const wav = encodeWav({ left, right, cues: [0, 1, 2] });
  assert.equal(checkUploadable(wav, { maxFrames: 3, maxCues: 2 }).length, 2);
  const mono = wav.slice();
  new DataView(mono.buffer).setUint16(22, 1, true);
  assert.match(checkUploadable(mono)[0], /stereo/);
});
