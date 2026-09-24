/**
 * Contrato BLE con el llavero GEO-EXPO ALERT.
 *
 * Fuente de verdad: ../../INTEGRACION-APP.md (congelado y verificado en hardware).
 * Este módulo es TypeScript puro, sin React Native, para poder probarlo con
 * `npm test` sin un teléfono delante.
 */

/** Nordic UART Service. Filtramos el escaneo por este UUID, no por nombre. */
export const NUS_SERVICE = '6E400001-B5A3-F393-E0A9-E50E24DCCA9E';
/** Dispositivo → app (NOTIFY). Hay que suscribirse o no llega nada. */
export const NUS_TX = '6E400003-B5A3-F393-E0A9-E50E24DCCA9E';
/** App → dispositivo (WRITE / WRITE_NO_RESPONSE). */
export const NUS_RX = '6E400002-B5A3-F393-E0A9-E50E24DCCA9E';

export const DEVICE_NAME = 'GEOEXPO-ALERT';

/** MTU a pedir nada más conectar (§6): el mensaje con GPS no cabe en 20 bytes. */
export const REQUESTED_MTU = 185;

/**
 * Ventana de cancelación del firmware (§5). La cuenta atrás de la app nunca
 * puede ser más corta: avisaríamos antes de que el usuario pudiera cancelar.
 */
export const DEVICE_CANCEL_WINDOW_MS = 10_000;

export type AlertLevel = 1 | 2;

export interface Position {
  lat: number;
  lon: number;
}

export type DeviceMessage =
  | { kind: 'alert'; level: AlertLevel; position: Position | null }
  | { kind: 'cancel' };

/**
 * Interpreta una línea ya separada por `\n`. Devuelve `null` para lo que no
 * conocemos: la app debe ignorarlo en silencio (§6) para que el firmware pueda
 * crecer sin romperla.
 *
 * Acepta ya el formato con posición propuesto en §7 (`ALERT:1;lat;lon`), que
 * es compatible hacia atrás: sin `;` se comporta exactamente como hoy.
 */
export function parseDeviceLine(raw: string): DeviceMessage | null {
  const line = raw.trim();
  if (line.length === 0) return null;

  const [head, ...rest] = line.split(';');
  const cmd = head.trim().toUpperCase();

  if (cmd === 'CANCEL') return { kind: 'cancel' };

  const m = /^ALERT:([12])$/.exec(cmd);
  if (!m) return null;

  const level = Number(m[1]) as AlertLevel;
  return { kind: 'alert', level, position: parsePosition(rest) };
}

function parsePosition(parts: string[]): Position | null {
  if (parts.length < 2) return null;
  const lat = Number(parts[0]);
  const lon = Number(parts[1]);
  if (!Number.isFinite(lat) || !Number.isFinite(lon)) return null;
  if (parts[0].trim() === '' || parts[1].trim() === '') return null;
  if (Math.abs(lat) > 90 || Math.abs(lon) > 180) return null;
  return { lat, lon };
}

/**
 * Acumula notificaciones y las parte por `\n` (§6). Hoy cada notificación trae
 * un mensaje entero, pero no hay que darlo por hecho en un perfil serie.
 */
export class LineBuffer {
  private pending = '';
  private readonly maxPending: number;

  /** Límite defensivo: si nunca llega `\n`, no crecemos sin fin. */
  constructor(maxPending = 512) {
    this.maxPending = maxPending;
  }

  push(chunk: string): string[] {
    this.pending += chunk;
    const parts = this.pending.split(/\r?\n/);
    this.pending = parts.pop() ?? '';
    if (this.pending.length > this.maxPending) this.pending = '';
    return parts.filter((p) => p.trim().length > 0);
  }

  reset(): void {
    this.pending = '';
  }
}

export type AppCommand = 'BEACON:ON' | 'BEACON:OFF';

export function beaconCommand(on: boolean): AppCommand {
  return on ? 'BEACON:ON' : 'BEACON:OFF';
}

// --- base64 para ASCII -----------------------------------------------------
// react-native-ble-plx intercambia los valores en base64. Los mensajes son
// ASCII puro, así que una implementación mínima nos ahorra una dependencia y
// no depende de que el motor JS traiga atob/btoa.

const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

export function asciiToBase64(text: string): string {
  let out = '';
  for (let i = 0; i < text.length; i += 3) {
    const a = text.charCodeAt(i) & 0xff;
    const b = i + 1 < text.length ? text.charCodeAt(i + 1) & 0xff : NaN;
    const c = i + 2 < text.length ? text.charCodeAt(i + 2) & 0xff : NaN;
    const n = (a << 16) | ((b || 0) << 8) | (c || 0);
    out += B64[(n >> 18) & 63] + B64[(n >> 12) & 63];
    out += Number.isNaN(b) ? '=' : B64[(n >> 6) & 63];
    out += Number.isNaN(c) ? '=' : B64[n & 63];
  }
  return out;
}

export function base64ToAscii(b64: string): string {
  const clean = b64.replace(/[^A-Za-z0-9+/]/g, '');
  let out = '';
  let buffer = 0;
  let bits = 0;
  for (const ch of clean) {
    buffer = (buffer << 6) | B64.indexOf(ch);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += String.fromCharCode((buffer >> bits) & 0xff);
    }
  }
  return out;
}
