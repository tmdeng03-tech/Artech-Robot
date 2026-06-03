import serial
import requests
import time
import re

# =========================
# CONFIG
# =========================

SERIAL_PORT = "COM3"
BAUD_RATE = 9600

SERVER_IP = "192.168.1.85"
SERVER_PORT = 5000

DATA_URL = f"http://{SERVER_IP}:{SERVER_PORT}/api/robot-string"
COMMAND_URL = f"http://{SERVER_IP}:{SERVER_PORT}/api/latest-command"

last_command_id = -1

# =========================
# SEND DATA TO SERVER
# =========================

def send_robot_data(raw_string):

    payload = {
        "raw": raw_string
    }

    try:

        response = requests.post(
            DATA_URL,
            json=payload,
            timeout=3
        )

        if response.status_code == 200:

            print("Đã gửi dữ liệu lên web:", raw_string)

        else:

            print("Server lỗi:", response.status_code)
            print(response.text)

    except Exception as e:

        print("Lỗi gửi data:", e)

# =========================
# GET COMMAND FROM SERVER
# =========================

def get_latest_command():

    try:

        response = requests.get(
            COMMAND_URL,
            timeout=2
        )

        if response.status_code == 200:

            return response.json()

    except Exception as e:

        print("Lỗi lấy command:", e)

    return None

# =========================
# SEND COMMAND TO ARDUINO
# =========================

def send_command_to_arduino(
    arduino,
    command
):

    if not command:
        return

    try:

        arduino.write(
            command.encode("utf-8")
        )

        print("Đã gửi lệnh xuống Arduino:", command)

    except Exception as e:

        print("Lỗi gửi UART:", e)

# =========================
# MAIN
# =========================

def main():

    global last_command_id

    try:

        arduino = serial.Serial(
            SERIAL_PORT,
            BAUD_RATE,
            timeout=0.1
        )

        time.sleep(2)

        print("Đã kết nối Arduino")
        print("--------------------------------")

        last_send_time = 0
        last_poll_time = 0

        while True:

            # =====================================
            # ĐỌC UART TỪ ARDUINO
            # =====================================

            try:

                line = arduino.readline() \
                    .decode(
                        "utf-8",
                        errors="ignore"
                    ) \
                    .strip()

                # Chỉ xử lý dòng feedback đúng format
                # Ví dụ:
                # L100R100B390T31h

                if line.startswith("L"):

                    match = re.search(
                        r"L(-?\d+)R(-?\d+)B(-?\d+)T(-?\d+)h",
                        line
                    )

                    if match:

                        speed_left = match.group(1)
                        speed_right = match.group(2)
                        battery = match.group(3)
                        temperature = match.group(4)

                        print("\n===== ROBOT DATA =====")

                        print("Left Speed :", speed_left)
                        print("Right Speed:", speed_right)
                        print("Battery    :", battery)
                        print("Temp       :", temperature)

                        print("RAW UART   :", line)

                        # Gửi lên server mỗi 0.5s
                        if time.time() - last_send_time >= 0.5:

                            send_robot_data(line)

                            last_send_time = time.time()

                else:

                    # Debug UART khác
                    if line:
                        print("DEBUG UART:", line)

            except Exception as e:

                print("Lỗi đọc UART:", e)

            # =====================================
            # LẤY COMMAND TỪ WEBSERVER
            # =====================================

            try:

                if time.time() - last_poll_time >= 0.2:

                    command_data = get_latest_command()

                    last_poll_time = time.time()

                    if command_data:

                        command_id = command_data.get("id")

                        serial_command = command_data.get(
                            "serialCommand"
                        )

                        # Chỉ gửi command mới
                        if (
                            command_id is not None and
                            command_id != last_command_id
                        ):

                            last_command_id = command_id

                            print("\nLệnh từ webserver:")

                            print(command_data)

                            send_command_to_arduino(
                                arduino,
                                serial_command
                            )

            except Exception as e:

                print("Lỗi command:", e)

            time.sleep(0.05)

    except serial.SerialException as e:

        print("Không mở được COM:", e)

    except KeyboardInterrupt:

        print("Đã dừng chương trình")

# =========================

if __name__ == "__main__":
    main()