from flask import Flask, request, jsonify
from flask_cors import CORS
from flask_socketio import SocketIO, emit
from gestures import GestureRecognizer
import time
import math

# =========================
# CONFIG
# =========================
JERK_THRESHOLD = 0.6     # порог рывка по оси
COOLDOWN = 0.25           # анти-спам (сек)

# =========================
# Flask + SocketIO
# =========================
app = Flask(__name__)
CORS(app)
socketio = SocketIO(app, cors_allowed_origins="*")

recognizer = GestureRecognizer()

packet_count = 0

# =========================
# JERK STATE
# =========================
prev_accel = {"x": 0.0, "y": 0.0, "z": 0.0}
last_trigger_time = 0.0


# -------------------------
# Главная страница (UI)
# -------------------------
@app.route("/")
def index():
    return """
<!DOCTYPE html>
<html>
<head>
  <title>Gesture Server</title>
  <script src="https://cdn.socket.io/4.7.2/socket.io.min.js"></script>
</head>
<body>
  <h2>🖐 Gesture Server</h2>

  <p>Packets: <span id="packets">0</span></p>
  <p>Recording: <span id="recording">false</span></p>
  <p>Last gesture: <b><span id="gesture">—</span></b></p>

  <input id="name" placeholder="gesture name"/>
  <button onclick="record()">🎬 Record gesture</button>

  <script>
    const socket = io();

    socket.on("state", data => {
      document.getElementById("packets").innerText = data.packets;
      document.getElementById("recording").innerText = data.recording;
      document.getElementById("gesture").innerText = data.last_gesture || "—";
    });

    function record() {
      const name = document.getElementById("name").value;
      socket.emit("start_record", {name: name});
    }
  </script>
</body>
</html>
"""


# -------------------------
# Приём данных от ESP
# -------------------------
@app.route("/api/data", methods=["POST"])
def receive_data():
    global packet_count, last_trigger_time

    data = request.get_json()
    packet_count += 1

    angle = (
        data["angle_x"],
        data["angle_y"],
        data["angle_z"]
    )

    ax = data["accel_x"]
    ay = data["accel_y"]
    az = data["accel_z"]

    # ===== JERK DETECTOR (AXIS + DIRECTION) =====
    dx = ax - prev_accel["x"]
    dy = ay - prev_accel["y"]
    dz = az - prev_accel["z"]

    prev_accel["x"] = ax
    prev_accel["y"] = ay
    prev_accel["z"] = az

    jerk_x = abs(dx)
    jerk_y = abs(dy)
    jerk_z = abs(dz)

    jerk_max = max(jerk_x, jerk_y, jerk_z)
    now = time.time()

    if jerk_max > JERK_THRESHOLD and now - last_trigger_time > COOLDOWN:
        last_trigger_time = now

        if jerk_max == jerk_x:
            axis = "X"
            direction = "+" if dx > 0 else "-"
        elif jerk_max == jerk_y:
            axis = "Y"
            direction = "+" if dy > 0 else "-"
        else:
            axis = "Z"
            direction = "+" if dz > 0 else "-"

        print(
            f"💥 JERK | axis={axis}{direction} | "
            f"dx={dx:.2f}, dy={dy:.2f}, dz={dz:.2f}"
        )

    # ===== Gesture logic =====
    recognizer.add_data(angle, (ax, ay, az))
    gesture = recognizer.recognize()

    socketio.emit("state", {
        "packets": packet_count,
        "recording": recognizer.recording,
        "last_gesture": recognizer.active_gesture
    })

    if gesture:
        print(f"🎉 Gesture detected: {gesture}")

    return jsonify({"status": "ok"})


# -------------------------
# WebSocket handlers
# -------------------------
@socketio.on("start_record")
def start_record(data):
    name = data.get("name", "gesture")
    recognizer.start_record(name)

    emit("state", {
        "packets": packet_count,
        "recording": True,
        "last_gesture": recognizer.active_gesture
    }, broadcast=True)


# -------------------------
# Запуск
# -------------------------
if __name__ == "__main__":
    print("🚀 Server running on 0.0.0.0:8000")
    socketio.run(app, host="0.0.0.0", port=8000)
