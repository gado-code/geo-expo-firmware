"""Prueba automatica de la seccion 3 de PRUEBAS.md (BLE) desde el Bluetooth del PC.

Cruza lo que llega por BLE (bleak) con el log serie (pyserial) y simula el
boton BOOT por DTR, igual que tools/hw.py.
Uso:  python ble_test.py COM3
"""
import asyncio, sys, threading, time
import serial
from bleak import BleakClient, BleakScanner

NAME = "GEOEXPO-ALERT"
SVC  = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
RX   = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # app -> ESP32 (WRITE)
TX   = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # ESP32 -> app (NOTIFY)

T0 = time.perf_counter()
def ms(): return int((time.perf_counter() - T0) * 1000)

serial_lines, notes, results = [], [], []
lock = threading.Lock()

def log(s):
    with lock:
        print(f"{ms():7d} {s}", flush=True)

ser = serial.Serial(); ser.port = sys.argv[1]; ser.baudrate = 115200; ser.timeout = 0.05
ser.dtr = False; ser.rts = False; ser.open()
stop = False
def reader():
    buf = b""
    while not stop:
        d = ser.read(256)
        if not d: continue
        buf += d
        while b"\n" in buf:
            ln, buf = buf.split(b"\n", 1)
            t = ln.decode("utf-8", "replace").rstrip()
            if t:
                with lock: serial_lines.append((ms(), t))
                log(f"SER | {t}")
threading.Thread(target=reader, daemon=True).start()

def press(hold_ms):
    log(f"#   PULSAR {hold_ms} ms")
    ser.rts = False; ser.dtr = True
    end = time.perf_counter() + hold_ms / 1000
    while time.perf_counter() < end: pass
    ser.dtr = False
    log("#   SOLTAR")

def serial_has(text, since):
    with lock: return any(t0 >= since and text in t for t0, t in serial_lines)

def notes_since(since):
    with lock: return [(t, v) for t, v in notes if t >= since]

def check(case, ok, detail=""):
    results.append((case, ok, detail))
    log(f"==> {case}: {'OK' if ok else 'FALLO'} {detail}")

def on_notify(_, data: bytearray):
    v = bytes(data).decode("utf-8", "replace")
    with lock: notes.append((ms(), v))
    log(f"BLE | NOTIFY {v!r}")

async def scan():
    return await BleakScanner.find_device_by_filter(
        lambda d, ad: (d.name == NAME) or (ad.local_name == NAME) or (SVC in [u.lower() for u in ad.service_uuids]),
        timeout=20)

async def main():
    # 3.1 escaneo
    dev = await scan()
    check("3.1 visible en escaneo", dev is not None, f"{dev.address if dev else ''}")
    if not dev: return

    # 3.2 conexion
    t = ms()
    async with BleakClient(dev) as c:
        await asyncio.sleep(1.5)
        check("3.2 conexion", c.is_connected and serial_has("cliente CONECTADO", t))

        # 3.3 servicio y caracteristicas
        svc = c.services.get_service(SVC)
        tx = svc.get_characteristic(TX) if svc else None
        rx = svc.get_characteristic(RX) if svc else None
        check("3.3 servicio y caracteristicas",
              bool(tx and rx and "notify" in tx.properties and "write" in rx.properties),
              f"TX={tx.properties if tx else None} RX={rx.properties if rx else None}")

        # 3.4 suscripcion
        await c.start_notify(TX, on_notify)
        await asyncio.sleep(0.5)
        check("3.4 suscripcion a TX", True)

        # 3.5 ALERT:1
        t = ms(); press(300); await asyncio.sleep(1.5)
        got = [v for _, v in notes_since(t)]
        check("3.5 recibir ALERT:1", got == ["ALERT:1\n"], repr(got))
        await asyncio.sleep(10)   # dejar expirar la ventana

        # 3.6 ALERT:2 y 3.7 CANCEL
        t = ms(); press(3500)
        n = notes_since(t)
        check("3.6 recibir ALERT:2", [v for _, v in n] == ["ALERT:2\n"],
              f"{[v for _, v in n]!r} a los {n[0][0] - t if n else '-'} ms de pulsar")
        await asyncio.sleep(1.0)
        t = ms(); press(300); await asyncio.sleep(1.0)
        got = [v for _, v in notes_since(t)]
        check("3.7 recibir CANCEL", got == ["CANCEL\n"], repr(got))
        await asyncio.sleep(1.0)

        # 3.8 / 3.9 BEACON por RX
        t = ms(); await c.write_gatt_char(RX, b"BEACON:ON", response=True); await asyncio.sleep(1.0)
        check("3.8 enviar BEACON:ON", serial_has('<<< RX (BLE) "BEACON:ON"', t) and serial_has("baliza ACTIVADA", t))
        t = ms(); await c.write_gatt_char(RX, b"BEACON:OFF", response=True); await asyncio.sleep(1.0)
        check("3.9 enviar BEACON:OFF", serial_has('<<< RX (BLE) "BEACON:OFF"', t) and serial_has("baliza DESACTIVADA", t))

        # I15: dos WRITE seguidos sin respuesta no deben perderse
        t = ms()
        await c.write_gatt_char(RX, b"BEACON:ON", response=False)
        await c.write_gatt_char(RX, b"BEACON:OFF", response=False)
        await asyncio.sleep(1.0)
        check("I15 dos WRITE seguidos", serial_has('"BEACON:ON"', t) and serial_has('"BEACON:OFF"', t)
              and not serial_has("perdidos", t))

        await c.stop_notify(TX)
        t_disc = ms()

    # 3.10 desconexion y re-advertising
    await asyncio.sleep(1.5)
    readv = serial_has("DESCONECTADO", t_disc)
    dev2 = await scan()
    check("3.10 desconexion y re-advertising", readv and dev2 is not None)

    # 3.11 sin cliente
    t = ms(); press(300); await asyncio.sleep(1.5)
    check("3.11 funciona sin cliente", serial_has("registrado solo en serie", t) and not notes_since(t))
    await asyncio.sleep(10)

    # 3.12 reconexion
    dev3 = dev2 or await scan()
    async with BleakClient(dev3) as c:
        await asyncio.sleep(1.5)
        await c.start_notify(TX, on_notify)
        await asyncio.sleep(0.5)
        t = ms(); press(300); await asyncio.sleep(1.5)
        got = [v for _, v in notes_since(t)]
        check("3.12 reconexion", got == ["ALERT:1\n"], repr(got))
        await c.stop_notify(TX)
    await asyncio.sleep(11)

try:
    asyncio.run(main())
finally:
    stop = True; time.sleep(0.2); ser.close()
    print("\n===== RESUMEN =====")
    for case, ok, detail in results:
        print(f"{'OK   ' if ok else 'FALLO'} {case}  {detail}")
