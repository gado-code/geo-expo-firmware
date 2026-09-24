import AsyncStorage from '@react-native-async-storage/async-storage';

import type { AlertLevel, Position } from '@/protocol/nus.ts';
import type { AlertSource } from '@/protocol/alertMachine.ts';

export interface EmergencyContact {
  id: string;
  name: string;
  phone: string;
}

export type AlertOutcome = 'confirmed' | 'cancelled-device' | 'cancelled-app';

export interface HistoryEntry {
  id: string;
  level: AlertLevel;
  source: AlertSource;
  startedAt: number;
  resolvedAt: number;
  outcome: AlertOutcome;
  position: Position | null;
}

export interface Settings {
  /** Nunca por debajo de 10 s: ver effectiveCountdownMs. */
  countdownSeconds: number;
  shareLocation: boolean;
  message: string;
  /** Número de emergencias para el botón de llamada (123 en Colombia). */
  emergencyNumber: string;
  haptics: boolean;
}

export const defaultSettings: Settings = {
  countdownSeconds: 10,
  shareLocation: true,
  message: 'Necesito ayuda. He activado mi alerta Ivy.',
  emergencyNumber: '123',
  haptics: true,
};

const KEYS = {
  contacts: 'ivy/contacts/v1',
  history: 'ivy/history/v1',
  settings: 'ivy/settings/v1',
  device: 'ivy/device/v1',
} as const;

const HISTORY_LIMIT = 200;

async function readJson<T>(key: string, fallback: T): Promise<T> {
  try {
    const raw = await AsyncStorage.getItem(key);
    return raw ? (JSON.parse(raw) as T) : fallback;
  } catch {
    return fallback;
  }
}

async function writeJson(key: string, value: unknown): Promise<void> {
  try {
    await AsyncStorage.setItem(key, JSON.stringify(value));
  } catch {
    // Almacenamiento lleno o no disponible: la app sigue funcionando en memoria.
  }
}

export interface KnownDevice {
  id: string;
  name: string | null;
}

export const storage = {
  loadContacts: () => readJson<EmergencyContact[]>(KEYS.contacts, []),
  saveContacts: (c: EmergencyContact[]) => writeJson(KEYS.contacts, c),

  loadHistory: () => readJson<HistoryEntry[]>(KEYS.history, []),
  saveHistory: (h: HistoryEntry[]) => writeJson(KEYS.history, h.slice(0, HISTORY_LIMIT)),

  loadSettings: async () => ({ ...defaultSettings, ...(await readJson<Partial<Settings>>(KEYS.settings, {})) }),
  saveSettings: (s: Settings) => writeJson(KEYS.settings, s),

  loadDevice: () => readJson<KnownDevice | null>(KEYS.device, null),
  saveDevice: (d: KnownDevice | null) =>
    d ? writeJson(KEYS.device, d) : AsyncStorage.removeItem(KEYS.device).catch(() => {}),
};

export function newId(): string {
  return `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
}
