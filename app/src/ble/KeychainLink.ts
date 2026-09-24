/**
 * Conexión BLE con el llavero. Implementa §2, §3 y §6 de INTEGRACION-APP.md:
 *
 *  - escaneo filtrado por el UUID del servicio NUS, nunca por nombre ni MAC;
 *  - MTU 185 nada más conectar (Android; iOS lo negocia solo);
 *  - suscripción a TX (ble-plx escribe el CCCD 0x2902 por nosotros);
 *  - buffer de líneas partido por `\n`;
 *  - reconexión automática con espera creciente (el error 133 de Android es
 *    endémico: suele entrar al segundo o tercer intento);
 *  - un único cliente: sólo conectamos al llavero vinculado.
 *
 * ble-plx necesita un development build: en Expo Go y en web no existe el
 * módulo nativo y el enlace se queda en `unavailable` sin romper la app.
 */
import { PermissionsAndroid, Platform } from 'react-native';
import type { BleManager as BleManagerType, Device, Subscription } from 'react-native-ble-plx';

import {
  LineBuffer,
  NUS_RX,
  NUS_SERVICE,
  NUS_TX,
  REQUESTED_MTU,
  asciiToBase64,
  base64ToAscii,
  beaconCommand,
  parseDeviceLine,
  type DeviceMessage,
} from '@/protocol/nus.ts';

export type LinkStatus =
  | 'unavailable' // sin módulo nativo (Expo Go / web)
  | 'unauthorized' // el usuario negó el permiso de Bluetooth
  | 'poweredOff' // Bluetooth apagado
  | 'unpaired' // no hay llavero vinculado todavía
  | 'searching' // buscando el llavero vinculado
  | 'connecting'
  | 'connected';

export interface FoundDevice {
  id: string;
  name: string | null;
  rssi: number | null;
}

export interface LinkSnapshot {
  status: LinkStatus;
  deviceId: string | null;
  deviceName: string | null;
  rssi: number | null;
  beacon: boolean;
  connectedAt: number | null;
  /** Intento de reconexión en curso (0 = primero). */
  attempt: number;
}

export interface LinkCallbacks {
  onSnapshot: (s: LinkSnapshot) => void;
  onMessage: (m: DeviceMessage) => void;
  onLog?: (line: string) => void;
}

const BACKOFF_MS = [1000, 2000, 4000, 8000, 15000, 30000];
const CONNECT_TIMEOUT_MS = 12_000;

function loadBleManager(): (new (opts?: object) => BleManagerType) | null {
  if (Platform.OS === 'web') return null;
  try {
    // require perezoso: si el módulo nativo no está (Expo Go) no queremos que
    // falle la importación de toda la app.
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    return require('react-native-ble-plx').BleManager;
  } catch {
    return null;
  }
}

export class KeychainLink {
  private manager: BleManagerType | null = null;
  private cb: LinkCallbacks;
  private snap: LinkSnapshot = {
    status: 'unpaired',
    deviceId: null,
    deviceName: null,
    rssi: null,
    beacon: false,
    connectedAt: null,
    attempt: 0,
  };
  private buffer = new LineBuffer();
  private subs: Subscription[] = [];
  private deviceSubs: Subscription[] = [];
  private retryTimer: ReturnType<typeof setTimeout> | null = null;
  private scanning = false;
  private poweredOn = false;
  private stopped = false;

  constructor(cb: LinkCallbacks) {
    this.cb = cb;
  }

  get snapshot(): LinkSnapshot {
    return this.snap;
  }

  // ---------------------------------------------------------------- ciclo de vida

  async start(known: { id: string; name: string | null } | null): Promise<void> {
    this.stopped = false;
    this.update({ deviceId: known?.id ?? null, deviceName: known?.name ?? null });

    const Manager = loadBleManager();
    if (!Manager) {
      this.update({ status: 'unavailable' });
      return;
    }
    try {
      this.manager = new Manager({
        // Restauración de estado en iOS: si el sistema mata la app en segundo
        // plano, la vuelve a lanzar cuando el llavero notifica.
        restoreStateIdentifier: 'geoexpo-keychain',
        restoreStateFunction: () => {},
      });
    } catch {
      this.update({ status: 'unavailable' });
      return;
    }

    if (!(await this.ensurePermissions())) {
      this.update({ status: 'unauthorized' });
      return;
    }

    this.subs.push(
      this.manager.onStateChange((state) => {
        this.poweredOn = state === 'PoweredOn';
        if (state === 'Unauthorized') this.update({ status: 'unauthorized' });
        else if (state === 'PoweredOff') {
          this.clearRetry();
          this.update({ status: 'poweredOff', connectedAt: null, rssi: null });
        } else if (state === 'Unsupported') this.update({ status: 'unavailable' });
        else if (this.poweredOn) this.resume();
      }, true),
    );
  }

  async stop(): Promise<void> {
    this.stopped = true;
    this.clearRetry();
    this.stopScan();
    this.deviceSubs.forEach((s) => s.remove());
    this.subs.forEach((s) => s.remove());
    this.deviceSubs = [];
    this.subs = [];
    const m = this.manager;
    this.manager = null;
    await m?.destroy().catch(() => {});
  }

  /** Vincula un llavero concreto (elegido en la pantalla de vinculación). */
  async pair(device: FoundDevice): Promise<void> {
    this.stopScan();
    await this.disconnectCurrent();
    this.update({ deviceId: device.id, deviceName: device.name, attempt: 0, beacon: false });
    this.resume();
  }

  async forget(): Promise<void> {
    this.clearRetry();
    this.stopScan();
    if (this.snap.beacon) await this.setBeacon(false).catch(() => {});
    await this.disconnectCurrent();
    this.update({
      status: this.manager ? 'unpaired' : this.snap.status,
      deviceId: null,
      deviceName: null,
      rssi: null,
      connectedAt: null,
      beacon: false,
      attempt: 0,
    });
  }

  // ---------------------------------------------------------------- vinculación

  /**
   * Escanea llaveros cercanos para que el usuario elija. Filtra por servicio:
   * hay varias unidades y cada una tiene su dirección (§2).
   */
  discover(onFound: (d: FoundDevice) => void): () => void {
    const m = this.manager;
    if (!m || !this.poweredOn) return () => {};
    this.clearRetry();
    this.stopScan();
    this.scanning = true;
    m.startDeviceScan([NUS_SERVICE], { allowDuplicates: true }, (error, device) => {
      if (error) {
        this.log(`scan: ${error.message}`);
        return;
      }
      if (device) onFound({ id: device.id, name: device.localName ?? device.name, rssi: device.rssi });
    });
    return () => {
      this.stopScan();
      this.resume();
    };
  }

  // ---------------------------------------------------------------- comandos

  async setBeacon(on: boolean): Promise<void> {
    this.update({ beacon: on });
    if (this.snap.status !== 'connected') return; // se sincroniza al reconectar
    await this.write(beaconCommand(on));
  }

  // ---------------------------------------------------------------- internos

  private resume(): void {
    if (this.stopped || !this.manager || !this.poweredOn) return;
    if (!this.snap.deviceId) {
      this.update({ status: 'unpaired' });
      return;
    }
    if (this.snap.status === 'connected' || this.snap.status === 'connecting') return;
    void this.connectLoop();
  }

  private async connectLoop(): Promise<void> {
    const m = this.manager;
    const id = this.snap.deviceId;
    if (!m || !id || this.stopped) return;
    this.update({ status: 'searching' });

    try {
      // Si el sistema ya lo tiene conectado (restauración de estado en iOS,
      // o reconexión tras volver del segundo plano), lo reaprovechamos.
      const already = (await m.connectedDevices([NUS_SERVICE])).find((d) => d.id === id);
      const device = already ?? (await this.findAndConnect(id));
      if (!device) return; // findAndConnect programa el reintento
      await this.setup(device);
    } catch (e) {
      this.log(`conexión fallida: ${(e as Error).message}`);
      this.scheduleRetry();
    }
  }

  /**
   * Busca el llavero vinculado por escaneo y conecta al verlo. Es el camino
   * más fiable en ambas plataformas: evita el connect() a ciegas, que en
   * Android se queda colgado y en iOS no caduca nunca.
   */
  private findAndConnect(id: string): Promise<Device | null> {
    const m = this.manager!;
    return new Promise((resolve, reject) => {
      let settled = false;
      const giveUp = setTimeout(() => {
        if (settled) return;
        settled = true;
        this.stopScan();
        this.scheduleRetry();
        resolve(null);
      }, 20_000);

      this.scanning = true;
      m.startDeviceScan([NUS_SERVICE], null, (error, found) => {
        if (settled) return;
        if (error) {
          settled = true;
          clearTimeout(giveUp);
          this.stopScan();
          reject(error);
          return;
        }
        if (!found || found.id !== id) return;
        settled = true;
        clearTimeout(giveUp);
        this.stopScan();
        this.update({ status: 'connecting', rssi: found.rssi, deviceName: found.localName ?? found.name ?? this.snap.deviceName });
        m.connectToDevice(id, { timeout: CONNECT_TIMEOUT_MS, requestMTU: REQUESTED_MTU })
          .then(resolve)
          .catch(reject);
      });
    });
  }

  private async setup(device: Device): Promise<void> {
    const m = this.manager!;
    this.update({ status: 'connecting' });
    const ready = await device.discoverAllServicesAndCharacteristics();
    if (Platform.OS === 'android') {
      await m.requestMTUForDevice(ready.id, REQUESTED_MTU).catch(() => {});
    }

    this.deviceSubs.forEach((s) => s.remove());
    this.buffer.reset();
    this.deviceSubs = [
      m.monitorCharacteristicForDevice(ready.id, NUS_SERVICE, NUS_TX, (error, ch) => {
        if (error) {
          this.log(`notify: ${error.message}`);
          return;
        }
        if (!ch?.value) return;
        for (const line of this.buffer.push(base64ToAscii(ch.value))) {
          this.log(`TX ${line}`);
          const msg = parseDeviceLine(line);
          if (msg) this.cb.onMessage(msg);
        }
      }),
      m.onDeviceDisconnected(ready.id, () => {
        this.log('desconectado');
        this.deviceSubs.forEach((s) => s.remove());
        this.deviceSubs = [];
        this.update({ status: 'searching', connectedAt: null });
        // El llavero vuelve a anunciarse solo al perder el cliente (§6).
        this.scheduleRetry(0);
      }),
    ];

    this.update({ status: 'connected', connectedAt: Date.now(), attempt: 0 });
    // La baliza sobrevive a la desconexión en el firmware: reenviamos el
    // estado deseado para que app y llavero no discrepen.
    await this.write(beaconCommand(this.snap.beacon)).catch(() => {});
    ready.readRSSI().then((d) => this.update({ rssi: d.rssi })).catch(() => {});
  }

  private async write(cmd: string): Promise<void> {
    const m = this.manager;
    const id = this.snap.deviceId;
    if (!m || !id) return;
    this.log(`RX ${cmd}`);
    await m.writeCharacteristicWithResponseForDevice(id, NUS_SERVICE, NUS_RX, asciiToBase64(`${cmd}\n`));
  }

  private scheduleRetry(delay?: number): void {
    if (this.stopped) return;
    this.clearRetry();
    const attempt = this.snap.attempt;
    const wait = delay ?? BACKOFF_MS[Math.min(attempt, BACKOFF_MS.length - 1)];
    this.update({ status: 'searching', attempt: attempt + 1 });
    this.retryTimer = setTimeout(() => {
      this.retryTimer = null;
      this.resume();
    }, wait);
  }

  private clearRetry(): void {
    if (this.retryTimer) clearTimeout(this.retryTimer);
    this.retryTimer = null;
  }

  private stopScan(): void {
    if (!this.scanning) return;
    this.scanning = false;
    void this.manager?.stopDeviceScan();
  }

  private async disconnectCurrent(): Promise<void> {
    const id = this.snap.deviceId;
    this.deviceSubs.forEach((s) => s.remove());
    this.deviceSubs = [];
    if (id && this.manager) await this.manager.cancelDeviceConnection(id).catch(() => {});
  }

  private async ensurePermissions(): Promise<boolean> {
    if (Platform.OS !== 'android') return true; // iOS pregunta solo al usar BLE
    const api = typeof Platform.Version === 'number' ? Platform.Version : 0;
    if (api >= 31) {
      // Android 12+: SCAN y CONNECT son permisos distintos. Con sólo SCAN se ve
      // el llavero pero conectar falla en silencio (incidencia I4).
      const r = await PermissionsAndroid.requestMultiple([
        PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
        PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
      ]);
      return Object.values(r).every((v) => v === PermissionsAndroid.RESULTS.GRANTED);
    }
    // Android 11 y anteriores: ubicación fina (y encendida) o el escaneo sale vacío.
    const r = await PermissionsAndroid.request(PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION);
    return r === PermissionsAndroid.RESULTS.GRANTED;
  }

  private update(patch: Partial<LinkSnapshot>): void {
    this.snap = { ...this.snap, ...patch };
    this.cb.onSnapshot(this.snap);
  }

  private log(line: string): void {
    this.cb.onLog?.(line);
  }
}
