const socket = io();

let robotChart = null;
let holdingCommand = null;
let holdInterval = null;

// =========================
// SOCKET REALTIME
// =========================

socket.on("robot-data", (data) => {
  updateCurrentData(data.current, data.latestCommand);
  updateAnalysis(data.analysis);
  updateAlerts(data.alerts);
  updateHistory(data.history);
  updateChart(data.history);
});

socket.on("robot-command", (commandData) => {
  setText("lastCommand", `${commandData.command} (${commandData.serialCommand})`);
});

// =========================
// KHỞI TẠO
// =========================

document.addEventListener("DOMContentLoaded", () => {
  initChart();
  setupHoldButtons();
  setupCustomCommandInput();
});

// =========================
// SETUP NÚT NHẤN GIỮ + LỆNH TÙY CHỈNH
// =========================

function setupHoldButtons() {
  const buttons = document.querySelectorAll(".hold-btn");

  buttons.forEach((button) => {
    const command = button.dataset.command;

    button.addEventListener("pointerdown", (event) => {
      event.preventDefault();

      try {
        button.setPointerCapture(event.pointerId);
      } catch (error) {
        // Bỏ qua nếu trình duyệt không hỗ trợ
      }

      startCommand(command);
    });

    button.addEventListener("pointerup", (event) => {
      event.preventDefault();
      stopCommand();
    });

    button.addEventListener("pointerleave", () => {
      stopCommand();
    });

    button.addEventListener("pointercancel", () => {
      stopCommand();
    });

    button.addEventListener("lostpointercapture", () => {
      stopCommand();
    });
  });

  window.addEventListener("pointerup", () => {
    stopCommand();
  });

  window.addEventListener("blur", () => {
    stopCommand();
  });

  document.addEventListener("contextmenu", (event) => {
    if (event.target.tagName === "BUTTON") {
      event.preventDefault();
    }
  });
}

// =========================
// NHẤN GIỮ ĐỂ XE CHẠY
// =========================

function startCommand(command) {
  if (!command) {
    return;
  }

  if (holdingCommand === command) {
    return;
  }

  holdingCommand = command;
  setActiveButton(command, true);

  // Gửi ngay lần đầu
  sendCommand(command);

  // Gửi lặp khi giữ nút
  holdInterval = setInterval(() => {
    sendCommand(command);
  }, 150);
}

// =========================
// NHẢ TAY ĐỂ XE DỪNG
// =========================

function stopCommand() {
  if (holdingCommand === null) {
    return;
  }

  const oldCommand = holdingCommand;
  holdingCommand = null;

  if (holdInterval) {
    clearInterval(holdInterval);
    holdInterval = null;
  }

  setActiveButton(oldCommand, false);

  // Nhả tay thì dừng
  sendCommand("STOP");
}

// =========================
// GỬI LỆNH LÊN SERVER
// =========================

function sendCommand(command) {
  console.log("Đang gửi lệnh:", command);

  fetch("/api/control", {
    method: "POST",
    headers: {
      "Content-Type": "application/json"
    },
    body: JSON.stringify({ command: command })
  })
    .then(async (res) => {
      const data = await res.json();

      if (!res.ok) {
        console.error("Server từ chối lệnh:", data);
        return;
      }

      console.log("Server đã nhận lệnh:", data);
    })
    .catch((error) => {
      console.error("Lỗi gửi lệnh:", error);
    });
}

function setActiveButton(command, isActive) {
  const button = document.querySelector(`.hold-btn[data-command="${command}"]`);

  if (!button) {
    return;
  }

  if (isActive) {
    button.classList.add("active-control");
  } else {
    button.classList.remove("active-control");
  }
}

// Cho HTML gọi được nút Dừng
window.sendCommand = sendCommand;
window.startCommand = startCommand;
window.stopCommand = stopCommand;
window.sendCustomCommand = sendCustomCommand;

function setupCustomCommandInput() {
  const sendCustomCommandBtn = document.getElementById("sendCustomCommandBtn");
  const customCommandInput = document.getElementById("customCommandInput");

  if (!sendCustomCommandBtn || !customCommandInput) {
    return;
  }

  sendCustomCommandBtn.addEventListener("click", () => {
    sendCustomCommand(customCommandInput.value.trim());
  });

  customCommandInput.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      event.preventDefault();
      sendCustomCommand(customCommandInput.value.trim());
    }
  });
}

function sendCustomCommand(rawCommand) {
  if (!rawCommand) {
    return;
  }

  console.log("Đang gửi lệnh tùy chỉnh:", rawCommand);

  fetch("/api/custom-command", {
    method: "POST",
    headers: {
      "Content-Type": "application/json"
    },
    body: JSON.stringify({ rawCommand })
  })
    .then(async (res) => {
      const data = await res.json();

      if (!res.ok) {
        console.error("Server từ chối lệnh tùy chỉnh:", data);
        return;
      }

      console.log("Server đã nhận lệnh tùy chỉnh:", data);
      const input = document.getElementById("customCommandInput");
      if (input) {
        input.value = "";
      }
    })
    .catch((error) => {
      console.error("Lỗi gửi lệnh tùy chỉnh:", error);
    });
}

// =========================
// HIỂN THỊ DỮ LIỆU
// =========================

function showValue(value, unit = "") {
  if (value === null || value === undefined || value === "--" || isNaN(value)) {
    return "--";
  }

  return `${value}${unit}`;
}

function setText(id, value) {
  const element = document.getElementById(id);

  if (element) {
    element.innerText = value;
  }
}

function updateCurrentData(current, latestCommand) {
  setText("speedLeft", showValue(current.speedLeft, " rpm"));
  setText("speedRight", showValue(current.speedRight, " rpm"));
  setText("speedAvg", showValue(current.speedAvg, " rpm"));
  setText("battery", showValue(current.battery, " V"));
  setText("temp", showValue(current.temp, " °C"));
  updateStatusText("Available");
  setText("rawData", current.raw || "--");

  if (latestCommand) {
    setText("lastCommand", `${latestCommand.command} (${latestCommand.serialCommand})`);
  }
}

function updateStatusText(status) {
  const statusEl = document.getElementById("status");

  if (!statusEl) {
    return;
  }

  statusEl.innerText = status;
  statusEl.classList.toggle("available", status === "Available");
  statusEl.classList.toggle("unavailable", status === "Unavailable");
}

function updateAnalysis(analysis) {
  if (!analysis) return;

  setText("speedLeftMin", analysis.speedLeft?.min ?? "--");
  setText("speedLeftMax", analysis.speedLeft?.max ?? "--");
  setText("speedLeftAvg", analysis.speedLeft?.avg ?? "--");

  setText("speedRightMin", analysis.speedRight?.min ?? "--");
  setText("speedRightMax", analysis.speedRight?.max ?? "--");
  setText("speedRightAvg", analysis.speedRight?.avg ?? "--");

  setText("speedAvgMin", analysis.speedAvg?.min ?? "--");
  setText("speedAvgMax", analysis.speedAvg?.max ?? "--");
  setText("speedAvgAvg", analysis.speedAvg?.avg ?? "--");

  setText("batteryMin", analysis.battery?.min ?? "--");
  setText("batteryMax", analysis.battery?.max ?? "--");
  setText("batteryAvg", analysis.battery?.avg ?? "--");

  setText("tempMin", analysis.temp?.min ?? "--");
  setText("tempMax", analysis.temp?.max ?? "--");
  setText("tempAvg", analysis.temp?.avg ?? "--");
}

function updateAlerts(alerts) {
  const alertBox = document.getElementById("alertBox");

  if (!alertBox) return;

  alertBox.innerHTML = "";

  alerts.forEach((alert) => {
    const div = document.createElement("div");

    if (alert.type === "danger") {
      div.className = "alert-danger";
    } else if (alert.type === "warning") {
      div.className = "alert-warning";
    } else {
      div.className = "alert-normal";
    }

    div.innerText = alert.message;
    alertBox.appendChild(div);
  });
}

function updateHistory(history) {
  const historyLog = document.getElementById("historyLog");

  if (!historyLog) return;

  historyLog.innerHTML = "";

  if (!history || history.length === 0) {
    historyLog.innerHTML = "Chưa có dữ liệu.";
    return;
  }

  const latest = [...history].reverse().slice(0, 20);

  latest.forEach((item) => {
    const div = document.createElement("div");
    div.className = "history-item";

    const time = new Date(item.time).toLocaleTimeString();

    div.innerText =
      `${time} | Raw: ${item.raw} | L: ${item.speedLeft} | R: ${item.speedRight} | B: ${item.battery} | T: ${item.temp} °C | AVG: ${item.speedAvg}`;

    historyLog.appendChild(div);
  });
}

// =========================
// BIỂU ĐỒ
// =========================

function initChart() {
  const ctx = document.getElementById("robotChart");

  if (!ctx) {
    return;
  }

  robotChart = new Chart(ctx, {
    type: "line",
    data: {
      labels: [],
      datasets: [
        {
          label: "Speed Left",
          data: [],
          tension: 0.3
        },
        {
          label: "Speed Right",
          data: [],
          tension: 0.3
        },
        {
          label: "Speed AVG",
          data: [],
          tension: 0.3
        },
        {
          label: "Battery",
          data: [],
          tension: 0.3
        },
        {
          label: "Temperature",
          data: [],
          tension: 0.3
        }
      ]
    },
    options: {
      responsive: true,
      animation: false,
      plugins: {
        legend: {
          display: true
        }
      },
      scales: {
        y: {
          beginAtZero: false
        }
      }
    }
  });
}

function updateChart(history) {
  if (!robotChart || !history) {
    return;
  }

  const latest = history.slice(-20);

  robotChart.data.labels = latest.map((item) => {
    return new Date(item.time).toLocaleTimeString();
  });

  robotChart.data.datasets[0].data = latest.map((item) => item.speedLeft);
  robotChart.data.datasets[1].data = latest.map((item) => item.speedRight);
  robotChart.data.datasets[2].data = latest.map((item) => item.speedAvg);
  robotChart.data.datasets[3].data = latest.map((item) => item.battery);
  robotChart.data.datasets[4].data = latest.map((item) => item.temp);

  robotChart.update();
}