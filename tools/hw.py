"""Arnes serie para GEO-EXPO ALERT (ESP32 DevKit V1, CP2102).

Pasos separados por ';':
  w<ms>    esperar leyendo
  r        reset por RTS (EN a bajo 120 ms) con DTR suelto -> arranque normal
  p<ms>    'pulsar BOOT' por DTR (DTR=1,RTS=0 -> GPIO0 a bajo) durante <ms>
  t<texto> enviar una linea por serie
Uso: python hw.py COM3 "r;w5000;p300;w1000;tBEACON:ON;w2000"
"""
import sys, time, threading, serial

port, script = sys.argv[1], sys.argv[2]
out = sys.argv[3] if len(sys.argv) > 3 else None
T0 = time.perf_counter()
lines = []
lock = threading.Lock()


def now_ms():
    return int((time.perf_counter() - T0) * 1000)


def emit(s):
    with lock:
        lines.append(s)
        print(s, flush=True)


def busy_wait(ms):
    end = time.perf_counter() + ms / 1000.0
    while time.perf_counter() < end:
        pass


ser = serial.Serial()
ser.port, ser.baudrate, ser.timeout = port, 115200, 0.05
ser.dtr = False
ser.rts = False
ser.open()

stop = False


def reader():
    buf = b""
    while not stop:
        data = ser.read(256)
        if not data:
            continue
        buf += data
        while b"\n" in buf:
            ln, buf = buf.split(b"\n", 1)
            emit(f"{now_ms():7d} | {ln.decode('utf-8', 'replace').rstrip()}")


th = threading.Thread(target=reader, daemon=True)
th.start()

for step in [s for s in script.split(";") if s]:
    k, arg = step[0], step[1:]
    if k == "w":
        time.sleep(int(arg) / 1000.0)
    elif k == "r":
        emit(f"{now_ms():7d} # RESET")
        ser.dtr = False
        ser.rts = True
        time.sleep(0.12)
        ser.rts = False
    elif k == "p":
        emit(f"{now_ms():7d} # PULSAR {arg} ms")
        ser.rts = False
        ser.dtr = True
        busy_wait(int(arg))
        ser.dtr = False
        emit(f"{now_ms():7d} # SOLTAR")
    elif k == "t":
        emit(f"{now_ms():7d} # TX '{arg}'")
        ser.write((arg + "\n").encode())
    else:
        emit(f"paso desconocido: {step}")

stop = True
th.join(1)
ser.close()
if out:
    with open(out, "a", encoding="utf-8") as f:
        f.write(f"===== {script}\n" + "\n".join(lines) + "\n")
