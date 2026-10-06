import serial, sys, time

port = sys.argv[1]
secs = float(sys.argv[2])
outfile = sys.argv[3]
reset = len(sys.argv) < 5 or sys.argv[4] != "noreset"

ser = serial.Serial(port, 115200, timeout=0.2)
if reset:
    # esptool-style hard reset: pulse EN via RTS
    ser.setDTR(False)
    ser.setRTS(True)
    time.sleep(0.1)
    ser.setRTS(False)

t0 = time.time()
with open(outfile, "w", encoding="utf-8", errors="replace") as f:
    while time.time() - t0 < secs:
        data = ser.read(4096)
        if data:
            text = data.decode("utf-8", errors="replace")
            f.write(text)
            f.flush()
            print(text, end="", flush=True)
ser.close()
