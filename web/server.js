const express = require("express");
const http = require("http");
const { Server } = require("socket.io");
const cors = require("cors");

const app = express();
const server = http.createServer(app);

const io = new Server(server, {
  cors: {
    origin: "*"
  }
});

app.use(cors());
app.use(express.json());
app.use(express.static("public"));

let robotData = {
  speedLeft: null,
  speedRight: null,
  speedAvg: null,
  speed: null,
  battery: null,
  temp: null,
  raw: "",
  status: "STOP",
  updatedAt: null
};

let latestCommand = {
  id: 0,
  command: "STOP",
  serialCommand: "x",
  time: new Date()
};

let history = [];

// Nhận dữ liệu từ Python: L38R102B353T43h
app.post("/api/robot-string", (req, res) => {
  const { raw } = req.body;

  if (!raw) {
    return res.status(400).json({
      message: "Thiếu chuỗi raw"
    });
  }

  const parsedData = parseRobotString(raw);

  if (!parsedData) {
    console.log("Chuỗi sai định dạng:", raw);

    return res.status(400).json({
      message: "Sai định dạng chuỗi. Cần dạng L75R117B233T30h",
      raw
    });
  }

  robotData = {
    ...robotData,
    speedLeft: parsedData.speedLeft,
    speedRight: parsedData.speedRight,
    speedAvg: parsedData.speedAvg,
    speed: parsedData.speedAvg,
    battery: parsedData.battery,
    temp: parsedData.temp,
    raw: String(raw).trim(),
    updatedAt: new Date()
  };

  saveHistory(robotData);

  io.emit("robot-data", getDashboardData());

  console.log("Đã nhận dữ liệu từ Python:", raw);
  console.log("Đã tách dữ liệu:", parsedData);

  res.json({
    message: "Đã nhận và tách dữ liệu thành công",
    raw: robotData.raw,
    data: robotData
  });
});

app.get("/api/robot-data", (req, res) => {
  res.json(getDashboardData());
});

// Web gửi lệnh điều khiển
app.post("/api/control", (req, res) => {
  const { command } = req.body;

  const commandMap = {
    FORWARD: "w",
    BACKWARD: "s",
    LEFT: "a",
    RIGHT: "d",
    CENTER: "f",
    STOP: "x"
  };

  if (!command || !commandMap[command]) {
    console.log("Lệnh không hợp lệ:", command);

    return res.status(400).json({
      message: "Lệnh không hợp lệ",
      receivedCommand: command,
      validCommands: Object.keys(commandMap)
    });
  }

  robotData.status = command;

  latestCommand = {
    id: latestCommand.id + 1,
    command,
    serialCommand: commandMap[command],
    time: new Date()
  };

  io.emit("robot-data", getDashboardData());
  io.emit("robot-command", latestCommand);

  console.log("Đã nhận lệnh từ web:", latestCommand);

  res.json({
    message: "Đã nhận lệnh điều khiển",
    command,
    serialCommand: commandMap[command],
    commandId: latestCommand.id
  });
});

// Web gửi lệnh tùy chỉnh trực tiếp xuống phần cứng
app.post("/api/custom-command", (req, res) => {
  const { rawCommand } = req.body;

  if (!rawCommand || typeof rawCommand !== "string") {
    console.log("Lệnh tùy chỉnh không hợp lệ:", rawCommand);

    return res.status(400).json({
      message: "Lệnh tùy chỉnh không hợp lệ",
      receivedCommand: rawCommand
    });
  }

  robotData.status = "CUSTOM";

  latestCommand = {
    id: latestCommand.id + 1,
    command: "CUSTOM",
    serialCommand: rawCommand,
    time: new Date()
  };

  io.emit("robot-data", getDashboardData());
  io.emit("robot-command", latestCommand);

  console.log("Đã nhận lệnh tùy chỉnh từ web:", latestCommand);

  res.json({
    message: "Đã nhận lệnh tùy chỉnh",
    rawCommand,
    commandId: latestCommand.id
  });
});

// Python máy 2 lấy lệnh mới nhất
app.get("/api/latest-command", (req, res) => {
  res.json(latestCommand);
});

io.on("connection", (socket) => {
  console.log("Client connected:", socket.id);
  socket.emit("robot-data", getDashboardData());

  socket.on("disconnect", () => {
    console.log("Client disconnected:", socket.id);
  });
});

function parseRobotString(raw) {
  const cleanRaw = String(raw).trim();

  const pattern = /^L(-?\d+(?:\.\d+)?)R(-?\d+(?:\.\d+)?)B(-?\d+(?:\.\d+)?)T(-?\d+(?:\.\d+)?)h$/i;

  const match = cleanRaw.match(pattern);

  if (!match) {
    return null;
  }

  const speedLeft = Number(match[1]);
  const speedRight = Number(match[2]);
  const battery = Number(match[3]);
  const temp = Number(match[4]);
  const speedAvg = Number(((speedLeft + speedRight) / 2).toFixed(2));

  return {
    speedLeft,
    speedRight,
    speedAvg,
    battery,
    temp
  };
}

function saveHistory(data) {
  history.push({
    speedLeft: data.speedLeft,
    speedRight: data.speedRight,
    speedAvg: data.speedAvg,
    speed: data.speedAvg,
    battery: data.battery,
    temp: data.temp,
    raw: data.raw,
    status: data.status,
    time: new Date()
  });

  if (history.length > 100) {
    history.shift();
  }
}

function calculateStats(values) {
  const validValues = values.filter(value => {
    return value !== null && value !== undefined && !isNaN(value);
  });

  if (validValues.length === 0) {
    return {
      min: "--",
      max: "--",
      avg: "--"
    };
  }

  const min = Math.min(...validValues);
  const max = Math.max(...validValues);
  const avg = validValues.reduce((sum, value) => sum + value, 0) / validValues.length;

  return {
    min,
    max,
    avg: Number(avg.toFixed(2))
  };
}

function calculateAnalysis() {
  return {
    speedLeft: calculateStats(history.map(item => item.speedLeft)),
    speedRight: calculateStats(history.map(item => item.speedRight)),
    speedAvg: calculateStats(history.map(item => item.speedAvg)),
    speed: calculateStats(history.map(item => item.speedAvg)),
    battery: calculateStats(history.map(item => item.battery)),
    temp: calculateStats(history.map(item => item.temp))
  };
}

function getAlerts() {
  const alerts = [];

  if (
    robotData.speedLeft === null ||
    robotData.speedRight === null ||
    robotData.battery === null ||
    robotData.temp === null
  ) {
    alerts.push({
      type: "warning",
      message: "Chưa nhận được dữ liệu từ Arduino."
    });

    return alerts;
  }

  if (robotData.temp >= 70) {
    alerts.push({
      type: "danger",
      message: "Cảnh báo: Nhiệt độ quá cao!"
    });
  } else if (robotData.temp >= 55) {
    alerts.push({
      type: "warning",
      message: "Nhiệt độ đang cao."
    });
  }

  if (robotData.battery <= 200) {
    alerts.push({
      type: "danger",
      message: "Cảnh báo: Pin thấp!"
    });
  } else if (robotData.battery <= 220) {
    alerts.push({
      type: "warning",
      message: "Pin đang thấp."
    });
  }

  if (Math.abs(robotData.speedLeft - robotData.speedRight) >= 80) {
    alerts.push({
      type: "warning",
      message: "Chênh lệch tốc độ hai bánh lớn."
    });
  }

  if (Math.abs(robotData.speedAvg) >= 250) {
    alerts.push({
      type: "warning",
      message: "Tốc độ trung bình đang cao."
    });
  }

  if (alerts.length === 0) {
    alerts.push({
      type: "normal",
      message: "Robot hoạt động bình thường."
    });
  }

  return alerts;
}

function getDashboardData() {
  return {
    current: robotData,
    latestCommand,
    analysis: calculateAnalysis(),
    alerts: getAlerts(),
    history
  };
}

const PORT = 5000;

server.listen(5000, '0.0.0.0', () => {
    console.log('Server running on port 5000');
});