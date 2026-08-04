import assert from "node:assert/strict";
import test from "node:test";

import {
  HuiyanStreamParser,
  buildMissDistanceFrame,
  parseMissDistanceFrame,
} from "../tools/tracker_dashboard/protocol.mjs";

test("parses a fragmented pixel-offset frame", () => {
  const frames = [];
  const errors = [];
  const parser = new HuiyanStreamParser({
    onFrame: (frame) => frames.push(frame),
    onError: (error) => errors.push(error),
  });
  const raw = buildMissDistanceFrame({
    valid: true,
    targetId: 3,
    offsetX: 25,
    offsetY: -12,
    boxWidth: 120,
    boxHeight: 80,
  });

  parser.push(raw.slice(0, 4));
  parser.push(raw.slice(4, 11));
  parser.push(raw.slice(11));

  assert.equal(errors.length, 0);
  assert.equal(frames.length, 1);
  const target = parseMissDistanceFrame(frames[0], 1280, 720);
  assert.equal(target.valid, true);
  assert.equal(target.targetId, 3);
  assert.equal(target.offsetX, 25);
  assert.equal(target.offsetY, -12);
  assert.equal(target.imageX, 665);
  assert.equal(target.imageY, 372);
  assert.equal(target.boxWidth, 120);
  assert.equal(target.boxHeight, 80);
});

test("parses angle mode as little-endian float values", () => {
  let frame;
  const parser = new HuiyanStreamParser({ onFrame: (value) => (frame = value) });
  parser.push(
    buildMissDistanceFrame({
      valid: true,
      angleMode: true,
      offsetX: 4.5,
      offsetY: -2.25,
    }),
  );
  const target = parseMissDistanceFrame(frame);
  assert.equal(target.angleMode, true);
  assert.equal(target.offsetX, 4.5);
  assert.equal(target.offsetY, -2.25);
  assert.ok(Math.abs(target.bearingXRad - Math.PI / 40) < 1e-6);
});

test("rejects a frame with a bad checksum and recovers", () => {
  const frames = [];
  const errors = [];
  const parser = new HuiyanStreamParser({
    onFrame: (frame) => frames.push(frame),
    onError: (error) => errors.push(error),
  });
  const bad = buildMissDistanceFrame({ offsetX: 1 });
  bad[bad.length - 2] ^= 0xff;
  const good = buildMissDistanceFrame({ offsetX: 2 });
  const combined = new Uint8Array(bad.length + good.length);
  combined.set(bad);
  combined.set(good, bad.length);
  parser.push(combined);

  assert.equal(errors.length, 1);
  assert.equal(errors[0].type, "checksum");
  assert.equal(frames.length, 1);
  assert.equal(parseMissDistanceFrame(frames[0]).offsetX, 2);
});
