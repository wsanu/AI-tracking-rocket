import {
  HuiyanStreamParser,
  MavlinkStreamParser,
  buildMissDistanceFrame,
  bytesToHex,
  parseMissDistanceFrame,
} from "./protocol.mjs";

const elements = {
  modeButtons: [...document.querySelectorAll("[data-input-mode]")],
  connectButton: document.querySelector("#connect-button"),
  browserState: document.querySelector("#browser-state"),
  connectionState: document.querySelector("#connection-state"),
  baudRate: document.querySelector("#baud-rate"),
  simulatorPanel: document.querySelector("#simulator-panel"),
  simulatorScenario: document.querySelector("#simulator-scenario"),
  simulatorRate: document.querySelector("#simulator-rate"),
  simulatorRateValue: document.querySelector("#simulator-rate-value"),
  simulatorAmplitude: document.querySelector("#simulator-amplitude"),
  simulatorAmplitudeValue: document.querySelector("#simulator-amplitude-value"),
  imageWidth: document.querySelector("#image-width"),
  imageHeight: document.querySelector("#image-height"),
  horizontalFov: document.querySelector("#horizontal-fov"),
  verticalFov: document.querySelector("#vertical-fov"),
  targetMarker: document.querySelector("#target-marker"),
  targetBox: document.querySelector("#target-box"),
  targetState: document.querySelector("#target-state"),
  stage: document.querySelector("#tracking-stage"),
  offsetX: document.querySelector("#offset-x"),
  offsetY: document.querySelector("#offset-y"),
  offsetUnit: document.querySelector("#offset-unit"),
  targetId: document.querySelector("#target-id"),
  targetSize: document.querySelector("#target-size"),
  inputProtocol: document.querySelector("#input-protocol"),
  packetRate: document.querySelector("#packet-rate"),
  frameCount: document.querySelector("#frame-count"),
  errorCount: document.querySelector("#error-count"),
  lastFrameTime: document.querySelector("#last-frame-time"),
  rawLog: document.querySelector("#raw-log"),
  pauseLog: document.querySelector("#pause-log"),
  clearLog: document.querySelector("#clear-log"),
  downloadLog: document.querySelector("#download-log"),
  emptyLog: document.querySelector("#empty-log"),
};

const state = {
  inputMode: "simulation",
  connected: false,
  port: null,
  reader: null,
  simulationTimer: null,
  simulationPhase: 0,
  frames: 0,
  errors: 0,
  frameTimes: [],
  logEntries: [],
};

const huiyanParser = new HuiyanStreamParser({
  onFrame: (frame) => {
    addFrame(frame);
    const target = parseMissDistanceFrame(frame, numericValue(elements.imageWidth), numericValue(elements.imageHeight));

    if (target) {
      displayTarget(target);
    }
  },
  onError: (error) => {
    state.errors += 1;
    updateCounters();
    addLogEntry({
      kind: "error",
      protocol: "慧眼",
      summary: error.type === "checksum" ? "校验和错误" : "帧尾错误",
      raw: error.raw,
    });
  },
});

const mavlinkParser = new MavlinkStreamParser({
  onTracker: displayTarget,
  onFrame: (frame) => {
    if (frame.messageId === 350) {
      addFrame(frame);
    }
  },
});

function numericValue(input) {
  return Number.parseFloat(input.value);
}

function setConnectionState(connected, label) {
  state.connected = connected;
  elements.connectionState.textContent = label;
  elements.connectionState.dataset.connected = String(connected);
  elements.connectButton.textContent = connected ? "断开" : "连接";
  elements.modeButtons.forEach((button) => {
    button.disabled = connected;
  });
  elements.baudRate.disabled = connected;
}

function setInputMode(mode) {
  if (state.connected) {
    return;
  }

  state.inputMode = mode;
  elements.modeButtons.forEach((button) => {
    const active = button.dataset.inputMode === mode;
    button.classList.toggle("active", active);
    button.setAttribute("aria-pressed", String(active));
  });
  elements.simulatorPanel.hidden = mode !== "simulation";
  elements.browserState.hidden = mode === "simulation" || "serial" in navigator;
}

async function toggleConnection() {
  if (state.connected) {
    await disconnect();
    return;
  }

  resetSession();

  if (state.inputMode === "simulation") {
    startSimulation();
  } else {
    await connectSerial();
  }
}

function resetSession() {
  state.frames = 0;
  state.errors = 0;
  state.frameTimes = [];
  huiyanParser.reset();
  mavlinkParser.reset();
  updateCounters();
}

function startSimulation() {
  setConnectionState(true, "模拟运行");
  scheduleSimulation();
}

function scheduleSimulation() {
  clearInterval(state.simulationTimer);
  const rate = Math.max(1, numericValue(elements.simulatorRate));
  state.simulationTimer = setInterval(emitSimulationFrame, 1000 / rate);
}

function emitSimulationFrame() {
  state.simulationPhase += 0.07;
  const scenario = elements.simulatorScenario.value;
  const angleMode = scenario === "angle";
  const valid = scenario !== "lost";
  const amplitude = numericValue(elements.simulatorAmplitude);
  const imageWidth = numericValue(elements.imageWidth);
  const imageHeight = numericValue(elements.imageHeight);
  const offsetX = Math.sin(state.simulationPhase) * (angleMode ? amplitude * 0.04 : imageWidth * amplitude * 0.004);
  const offsetY = Math.cos(state.simulationPhase * 0.73) * (angleMode ? amplitude * 0.03 : imageHeight * amplitude * 0.003);
  const pulse = (Math.sin(state.simulationPhase * 0.41) + 1) / 2;
  const frame = buildMissDistanceFrame({
    valid,
    running: true,
    angleMode,
    targetId: 1,
    offsetX,
    offsetY,
    boxWidth: valid ? 90 + pulse * 70 : 0,
    boxHeight: valid ? 60 + pulse * 45 : 0,
  });

  const split = 3 + (Math.floor(state.simulationPhase * 10) % Math.max(1, frame.length - 4));
  huiyanParser.push(frame.slice(0, split));
  huiyanParser.push(frame.slice(split));
}

async function connectSerial() {
  if (!("serial" in navigator)) {
    elements.browserState.hidden = false;
    return;
  }

  try {
    state.port = await navigator.serial.requestPort();
    await state.port.open({ baudRate: Number.parseInt(elements.baudRate.value, 10) });
    setConnectionState(true, "USB 已连接");
    void readSerialLoop();
  } catch (error) {
    setConnectionState(false, "未连接");

    if (error.name !== "NotFoundError") {
      addLogEntry({
        kind: "error",
        protocol: "系统",
        summary: error.message,
        raw: new Uint8Array(),
      });
    }
  }
}

async function readSerialLoop() {
  while (state.port?.readable && state.connected) {
    state.reader = state.port.readable.getReader();

    try {
      while (state.connected) {
        const { value, done } = await state.reader.read();

        if (done) {
          break;
        }

        if (value) {
          huiyanParser.push(value);
          mavlinkParser.push(value);
        }
      }
    } catch (error) {
      addLogEntry({
        kind: "error",
        protocol: "系统",
        summary: error.message,
        raw: new Uint8Array(),
      });
    } finally {
      state.reader.releaseLock();
      state.reader = null;
    }
  }

  if (state.connected) {
    await disconnect();
  }
}

async function disconnect() {
  clearInterval(state.simulationTimer);
  state.simulationTimer = null;
  setConnectionState(false, "未连接");

  if (state.reader) {
    await state.reader.cancel().catch(() => {});
  }

  if (state.port) {
    await state.port.close().catch(() => {});
    state.port = null;
  }
}

function addFrame(frame) {
  state.frames += 1;
  state.frameTimes.push(performance.now());
  const cutoff = performance.now() - 1000;
  state.frameTimes = state.frameTimes.filter((time) => time >= cutoff);
  updateCounters();

  const summary =
    frame.transport === "huiyan"
      ? `CMD ${frame.cmd0.toString(16).padStart(2, "0").toUpperCase()} ${frame.cmd1
          .toString(16)
          .padStart(2, "0")
          .toUpperCase()} · ${frame.payload.length} bytes`
      : `MAVLink message ${frame.messageId}`;

  addLogEntry({
    kind: "frame",
    protocol: frame.transport === "huiyan" ? "慧眼" : "MAVLink",
    summary,
    raw: frame.raw,
  });
}

function displayTarget(target) {
  const width = numericValue(elements.imageWidth);
  const height = numericValue(elements.imageHeight);
  const hfov = numericValue(elements.horizontalFov);
  const vfov = numericValue(elements.verticalFov);
  let x;
  let y;
  let unit;

  if (target.transport === "mavlink") {
    x = (target.bearingXRad * 180) / Math.PI;
    y = (target.bearingYRad * 180) / Math.PI;
    unit = "deg";
  } else {
    x = target.offsetX;
    y = target.offsetY;
    unit = target.angleMode ? "deg" : "px";
  }

  const xRange = unit === "deg" ? hfov / 2 : width / 2;
  const yRange = unit === "deg" ? vfov / 2 : height / 2;
  const xPercent = 50 + clamp(x / xRange, -1, 1) * 44;
  const yPercent = 50 - clamp(y / yRange, -1, 1) * 44;
  const boxWidthPercent = clamp((target.boxWidth / width) * 100, 2.5, 45);
  const boxHeightPercent = clamp((target.boxHeight / height) * 100, 3, 50);

  elements.targetMarker.style.left = `${xPercent}%`;
  elements.targetMarker.style.top = `${yPercent}%`;
  elements.targetBox.style.left = `${xPercent}%`;
  elements.targetBox.style.top = `${yPercent}%`;
  elements.targetBox.style.width = `${boxWidthPercent}%`;
  elements.targetBox.style.height = `${boxHeightPercent}%`;
  elements.targetMarker.hidden = !target.valid;
  elements.targetBox.hidden = !target.valid;
  elements.stage.dataset.valid = String(target.valid);
  elements.targetState.textContent = target.valid ? "目标有效" : "目标丢失";
  elements.offsetX.textContent = formatNumber(x);
  elements.offsetY.textContent = formatNumber(y);
  elements.offsetUnit.textContent = unit;
  elements.targetId.textContent = String(target.targetId ?? "--");
  elements.targetSize.textContent = `${Math.round(target.boxWidth)} × ${Math.round(target.boxHeight)}`;
  elements.inputProtocol.textContent = target.transport === "mavlink" ? "PX4 MAVLink" : "慧眼 V3.1";
  elements.lastFrameTime.textContent = new Date().toLocaleTimeString("zh-CN", { hour12: false });

  if (Number.isFinite(target.parseErrorCount)) {
    state.errors = target.parseErrorCount;
    updateCounters();
  }
}

function formatNumber(value) {
  if (!Number.isFinite(value)) {
    return "--";
  }

  return Math.abs(value) >= 100 ? value.toFixed(0) : value.toFixed(2);
}

function clamp(value, minimum, maximum) {
  return Math.min(maximum, Math.max(minimum, value));
}

function updateCounters() {
  elements.packetRate.textContent = `${state.frameTimes.length} Hz`;
  elements.frameCount.textContent = String(state.frames);
  elements.errorCount.textContent = String(state.errors);
}

function addLogEntry(entry) {
  const timestamp = new Date();
  state.logEntries.push({ ...entry, timestamp, hex: bytesToHex(entry.raw) });

  if (state.logEntries.length > 5000) {
    state.logEntries.shift();
  }

  if (elements.pauseLog.checked) {
    return;
  }

  elements.emptyLog.hidden = true;
  const row = document.createElement("tr");
  row.className = entry.kind === "error" ? "error-row" : "";
  const values = [
    timestamp.toLocaleTimeString("zh-CN", { hour12: false }),
    entry.protocol,
    entry.summary,
    bytesToHex(entry.raw),
  ];

  values.forEach((value, index) => {
    const cell = document.createElement("td");
    cell.textContent = value;

    if (index === 3) {
      cell.className = "raw-bytes";
    }

    row.append(cell);
  });
  elements.rawLog.prepend(row);

  while (elements.rawLog.children.length > 160) {
    elements.rawLog.lastElementChild.remove();
  }
}

function clearLog() {
  state.logEntries = [];
  elements.rawLog.replaceChildren();
  elements.emptyLog.hidden = false;
}

function downloadLog() {
  if (state.logEntries.length === 0) {
    return;
  }

  const lines = ["time,protocol,summary,hex"];

  for (const entry of state.logEntries) {
    const values = [entry.timestamp.toISOString(), entry.protocol, entry.summary, entry.hex].map(
      (value) => `"${String(value).replaceAll('"', '""')}"`,
    );
    lines.push(values.join(","));
  }

  const blob = new Blob([`${lines.join("\n")}\n`], { type: "text/csv;charset=utf-8" });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = `tracker-log-${new Date().toISOString().replaceAll(":", "-")}.csv`;
  anchor.click();
  URL.revokeObjectURL(url);
}

elements.modeButtons.forEach((button) => {
  button.addEventListener("click", () => setInputMode(button.dataset.inputMode));
});
elements.connectButton.addEventListener("click", toggleConnection);
elements.clearLog.addEventListener("click", clearLog);
elements.downloadLog.addEventListener("click", downloadLog);
elements.simulatorRate.addEventListener("input", () => {
  elements.simulatorRateValue.textContent = `${elements.simulatorRate.value} Hz`;

  if (state.connected && state.inputMode === "simulation") {
    scheduleSimulation();
  }
});
elements.simulatorAmplitude.addEventListener("input", () => {
  elements.simulatorAmplitudeValue.textContent = `${elements.simulatorAmplitude.value}%`;
});
navigator.serial?.addEventListener("disconnect", () => void disconnect());

setInputMode("simulation");
setConnectionState(false, "未连接");
elements.browserState.hidden = true;
