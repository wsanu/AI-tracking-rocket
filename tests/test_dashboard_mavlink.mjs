import assert from "node:assert/strict";
import test from "node:test";

import {
  MavlinkStreamParser,
  protocolConstants,
} from "../tools/tracker_dashboard/protocol.mjs";

function makeTrackerMavlinkFrame() {
  const payload = new Uint8Array(72);
  const view = new DataView(payload.buffer);
  view.setUint16(8, protocolConstants.TRACKER_DEBUG_ARRAY_ID, true);
  payload.set(new TextEncoder().encode(protocolConstants.TRACKER_DEBUG_ARRAY_NAME), 10);

  const data = [1, 1, 0, 7, 700, 310, 140, 90, 0.1, -0.05, 0.0137, 42, 3];
  data.forEach((value, index) => view.setFloat32(20 + index * 4, value, true));

  const frame = new Uint8Array(10 + payload.length + 2);
  frame.set([0xfd, payload.length, 0, 0, 1, 1, 1, 0x5e, 0x01, 0], 0);
  frame.set(payload, 10);
  return frame;
}

test("extracts tracker telemetry from fragmented MAVLink 2 data", () => {
  const trackerMessages = [];
  const parser = new MavlinkStreamParser({
    onTracker: (message) => trackerMessages.push(message),
  });
  const frame = makeTrackerMavlinkFrame();

  parser.push(frame.slice(0, 6));
  parser.push(frame.slice(6, 45));
  parser.push(frame.slice(45));

  assert.equal(trackerMessages.length, 1);
  const tracker = trackerMessages[0];
  assert.equal(tracker.valid, true);
  assert.equal(tracker.running, true);
  assert.equal(tracker.targetId, 7);
  assert.equal(tracker.imageX, 700);
  assert.equal(tracker.imageY, 310);
  assert.equal(tracker.boxWidth, 140);
  assert.equal(tracker.boxHeight, 90);
  assert.ok(Math.abs(tracker.bearingXRad - 0.1) < 1e-6);
  assert.equal(tracker.frameCount, 42);
  assert.equal(tracker.parseErrorCount, 3);
});

test("ignores an unrelated debug array", () => {
  const messages = [];
  const frame = makeTrackerMavlinkFrame();
  frame[10 + 8] = 0;
  frame[10 + 9] = 0;
  const parser = new MavlinkStreamParser({ onTracker: (message) => messages.push(message) });
  parser.push(frame);
  assert.equal(messages.length, 0);
});
