// Switching to transfer mode over a fake Web MIDI, with mocked timers. Run through run.sh.
import { test, mock, beforeEach, afterEach } from "node:test";
import assert from "node:assert/strict";
import { enterTransferMode, MidiPortNotFoundError } from "../../web/spotykach-transfer/index.js";

const never = new Promise(() => {});
const flush = () => new Promise(setImmediate);

function fakeNavigator({ permission = "granted", access = Promise.resolve(midiAccess([])) } = {}) {
  Object.defineProperty(globalThis, "navigator", {
    configurable: true,
    value: { permissions: { query: async () => ({ state: permission }) }, requestMIDIAccess: () => access },
  });
}

function midiAccess(ports) {
  const map = type => new Map(ports.filter(p => p.type === type).map((p, i) => [i, p]));
  return { inputs: map("input"), outputs: map("output") };
}

const spotykachPort = (type, open = async () => {}) => ({ type, name: "Spotykach", state: "connected", open, send() {} });

beforeEach(() => mock.timers.enable({ apis: ["setTimeout"] }));
afterEach(() => mock.timers.reset());

test("MIDI access that never comes times out", async () => {
  fakeNavigator({ access: never });
  const result = assert.rejects(enterTransferMode(), /didn't give access to MIDI within 8 s/);
  await flush();
  mock.timers.tick(8000);
  await result;
});

test("no time limit while the permission prompt is open", async () => {
  let grant;
  fakeNavigator({ permission: "prompt", access: new Promise(resolve => { grant = resolve; }) });
  const result = assert.rejects(enterTransferMode(), MidiPortNotFoundError);
  await flush();
  mock.timers.tick(60000);
  grant(midiAccess([]));
  await result;
});

test("a MIDI port that never opens times out", async () => {
  fakeNavigator({ access: Promise.resolve(midiAccess([spotykachPort("output", () => never), spotykachPort("input")])) });
  const result = assert.rejects(enterTransferMode(),
    e => /Can't open the Spotykach MIDI port/.test(e.message) && /within 5 s/.test(e.cause.message));
  await flush();
  mock.timers.tick(5000);
  await result;
});

test("each step of a switch is reported", async () => {
  const input = spotykachPort("input");
  const reply = () => input.onmidimessage({ data: [0xF0, 0x7D, 0x53, 0x4B, 0x02, 0, 0xF7] });
  const output = { ...spotykachPort("output"), send: () => queueMicrotask(reply) };
  fakeNavigator({ access: Promise.resolve(midiAccess([input, output])) });
  const steps = [];
  await enterTransferMode({ onStep: line => steps.push(line) });
  assert.deepEqual(steps, [
    "MIDI permission: granted",
    'MIDI ports: input "Spotykach" (connected), output "Spotykach" (connected)',
    "sending SysEx: enter transfer mode",
    "reply: OK",
  ]);
});

test("a MIDI access that comes after the time limit is reported", async () => {
  let grant;
  fakeNavigator({ access: new Promise(resolve => { grant = resolve; }) });
  const steps = [];
  const result = assert.rejects(enterTransferMode({ onStep: line => steps.push(line) }), /within 8 s/);
  await flush();
  mock.timers.tick(8000);
  await result;
  grant(midiAccess([]));
  await flush();
  assert.match(steps.at(-1), /^MIDI access came late, after \d+\.\d s$/);
});
