import * as Haptics from 'expo-haptics';
import * as Notifications from 'expo-notifications';
import { router } from 'expo-router';
import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
  type ReactNode,
} from 'react';
import { AppState } from 'react-native';

import { KeychainLink, type FoundDevice, type LinkSnapshot } from '@/ble/KeychainLink';
import {
  effectiveCountdownMs,
  initialAlertState,
  reduceAlert,
  type AlertEvent,
  type AlertState,
} from '@/protocol/alertMachine.ts';
import type { AlertLevel, DeviceMessage, Position } from '@/protocol/nus.ts';
import {
  callNumber,
  cancelNotifications,
  composeSms,
  currentPosition,
  scheduleAlertNotifications,
  setupNotifications,
} from '@/services/emergency';
import { buildAlertMessage } from '@/services/message.ts';
import {
  defaultSettings,
  newId,
  storage,
  type AlertOutcome,
  type EmergencyContact,
  type HistoryEntry,
  type Settings,
} from '@/state/storage';

export type SmsStatus = 'idle' | 'composing' | 'sent' | 'cancelled' | 'unavailable';

interface KeychainContextValue {
  ready: boolean;
  link: LinkSnapshot;
  alert: AlertState;
  /** Ubicación a enviar: la del llavero si trae GPS, si no la del teléfono. */
  alertPosition: Position | null;
  sms: SmsStatus;
  contacts: EmergencyContact[];
  history: HistoryEntry[];
  settings: Settings;
  log: string[];

  discover: (onFound: (d: FoundDevice) => void) => () => void;
  pair: (d: FoundDevice) => Promise<void>;
  forget: () => Promise<void>;
  setBeacon: (on: boolean) => Promise<void>;

  simulateAlert: (level: AlertLevel) => void;
  cancelAlert: () => void;
  confirmNow: () => void;
  dismissAlert: () => void;
  sendSms: () => Promise<void>;
  callEmergency: () => void;

  saveContact: (c: Omit<EmergencyContact, 'id'> & { id?: string }) => void;
  removeContact: (id: string) => void;
  updateSettings: (patch: Partial<Settings>) => void;
  clearHistory: () => void;
}

const KeychainContext = createContext<KeychainContextValue | null>(null);

export function useKeychain(): KeychainContextValue {
  const ctx = useContext(KeychainContext);
  if (!ctx) throw new Error('useKeychain fuera de <KeychainProvider>');
  return ctx;
}

const LOG_LIMIT = 80;

const initialLink: LinkSnapshot = {
  status: 'unpaired',
  deviceId: null,
  deviceName: null,
  rssi: null,
  beacon: false,
  connectedAt: null,
  attempt: 0,
};

export function KeychainProvider({ children }: { children: ReactNode }) {
  const [ready, setReady] = useState(false);
  const [link, setLink] = useState<LinkSnapshot>(initialLink);
  const [contacts, setContacts] = useState<EmergencyContact[]>([]);
  const [history, setHistory] = useState<HistoryEntry[]>([]);
  const [settings, setSettings] = useState<Settings>(defaultSettings);
  const [log, setLog] = useState<string[]>([]);
  const [alert, setAlert] = useState<AlertState>(initialAlertState);
  const [phonePosition, setPhonePosition] = useState<Position | null>(null);
  const [sms, setSms] = useState<SmsStatus>('idle');

  // Copias para leer desde callbacks de BLE, temporizadores y notificaciones.
  const settingsRef = useRef(settings);
  const contactsRef = useRef(contacts);
  const alertRef = useRef<AlertState>(initialAlertState);
  const phonePositionRef = useRef<Position | null>(null);
  const linkRef = useRef<KeychainLink | null>(null);
  const notifIds = useRef<string[]>([]);

  useEffect(() => {
    settingsRef.current = settings;
  }, [settings]);
  useEffect(() => {
    contactsRef.current = contacts;
  }, [contacts]);

  const pushLog = useCallback((line: string) => {
    const stamp = new Date().toLocaleTimeString();
    setLog((l) => [`${stamp}  ${line}`, ...l].slice(0, LOG_LIMIT));
  }, []);

  // ----------------------------------------------------------- ciclo de una alerta
  //
  // Toda transición pasa por aquí: se calcula el estado nuevo con la máquina
  // pura y se ejecutan los efectos de esa transición en el mismo momento.

  const send = useCallback(
    (event: AlertEvent) => {
      const prev = alertRef.current;
      const next = reduceAlert(prev, event, effectiveCountdownMs(settingsRef.current.countdownSeconds));
      if (next === prev) return;
      alertRef.current = next;
      setAlert(next);
      const haptics = settingsRef.current.haptics;

      // Empieza una alerta nueva.
      if (next.phase === 'countdown' && (prev.phase !== 'countdown' || prev.alert.id !== next.alert.id)) {
        const a = next.alert;
        setSms('idle');
        phonePositionRef.current = null;
        setPhonePosition(null);
        if (haptics) void Haptics.notificationAsync(Haptics.NotificationFeedbackType.Error);
        pushLog(`alerta nivel ${a.level} (${a.source === 'demo' ? 'demo' : 'llavero'})`);
        router.navigate('/alerta');
        void scheduleAlertNotifications(a.deadline, a.level).then((ids) => {
          notifIds.current = ids;
        });
        if (settingsRef.current.shareLocation) {
          void currentPosition().then((pos) => {
            const cur = alertRef.current;
            if (!pos || cur.phase === 'idle' || cur.alert.id !== a.id) return;
            phonePositionRef.current = pos;
            setPhonePosition(pos);
            // Si la alerta ya se cerró, completar su entrada del historial.
            setHistory((h) => h.map((x) => (x.id === a.id && !x.position ? { ...x, position: pos } : x)));
          });
        }
        return;
      }

      // Termina: cancelada o confirmada → historial.
      if (prev.phase === 'countdown' && (next.phase === 'cancelled' || next.phase === 'confirmed')) {
        const a = next.alert;
        let outcome: AlertOutcome;
        let resolvedAt: number;
        if (next.phase === 'cancelled') {
          outcome = next.by === 'device' ? 'cancelled-device' : 'cancelled-app';
          resolvedAt = next.cancelledAt;
          void cancelNotifications(notifIds.current);
          if (haptics) void Haptics.notificationAsync(Haptics.NotificationFeedbackType.Success);
          pushLog(outcome === 'cancelled-device' ? 'cancelada desde el llavero' : 'cancelada en la app');
        } else {
          outcome = 'confirmed';
          resolvedAt = next.confirmedAt;
          pushLog('alerta confirmada');
          // Con la app delante, la pantalla de alerta lleva al usuario a
          // avisar: la notificación programada sobra.
          if (AppState.currentState === 'active') void cancelNotifications(notifIds.current);
        }
        notifIds.current = [];
        const entry: HistoryEntry = {
          id: a.id,
          level: a.level,
          source: a.source,
          startedAt: a.startedAt,
          resolvedAt,
          outcome,
          position: a.devicePosition ?? phonePositionRef.current,
        };
        setHistory((h) => [entry, ...h.filter((x) => x.id !== entry.id)]);
      }
    },
    [pushLog],
  );

  // Cuenta atrás: TICK mientras dure. Si la app vuelve del segundo plano con el
  // plazo vencido, el primer TICK la confirma en el acto.
  const counting = alert.phase === 'countdown';
  useEffect(() => {
    if (!counting) return;
    const t = setInterval(() => send({ type: 'TICK', at: Date.now() }), 200);
    const sub = AppState.addEventListener('change', (s) => {
      if (s === 'active') send({ type: 'TICK', at: Date.now() });
    });
    return () => {
      clearInterval(t);
      sub.remove();
    };
  }, [counting, send]);

  // ----------------------------------------------------------- arranque

  useEffect(() => {
    let cancelled = false;
    const onMessage = (m: DeviceMessage) => {
      const at = Date.now();
      if (m.kind === 'cancel') send({ type: 'DEVICE_CANCEL', at });
      else send({ type: 'ALERT', level: m.level, position: m.position, source: 'device', at, id: newId() });
    };
    const l = new KeychainLink({ onSnapshot: setLink, onMessage, onLog: pushLog });
    linkRef.current = l;

    (async () => {
      const [c, h, s, d] = await Promise.all([
        storage.loadContacts(),
        storage.loadHistory(),
        storage.loadSettings(),
        storage.loadDevice(),
      ]);
      if (cancelled) return;
      setContacts(c);
      setHistory(h);
      setSettings(s);
      setReady(true);
      await setupNotifications();
      if (!cancelled) await l.start(d);
    })();

    const tap = Notifications.addNotificationResponseReceivedListener(() => router.navigate('/alerta'));
    return () => {
      cancelled = true;
      tap.remove();
      void l.stop();
    };
  }, [send, pushLog]);

  // ----------------------------------------------------------- persistencia

  useEffect(() => {
    if (ready) void storage.saveContacts(contacts);
  }, [ready, contacts]);
  useEffect(() => {
    if (ready) void storage.saveHistory(history);
  }, [ready, history]);
  useEffect(() => {
    if (ready) void storage.saveSettings(settings);
  }, [ready, settings]);
  useEffect(() => {
    if (ready) void storage.saveDevice(link.deviceId ? { id: link.deviceId, name: link.deviceName } : null);
  }, [ready, link.deviceId, link.deviceName]);

  // ----------------------------------------------------------- acciones

  const alertPosition = alert.phase === 'idle' ? null : (alert.alert.devicePosition ?? phonePosition);

  const sendSms = useCallback(async () => {
    const a = alertRef.current;
    if (a.phase === 'idle') return;
    const s = settingsRef.current;
    const body = buildAlertMessage({
      base: s.message,
      level: a.alert.level,
      position: s.shareLocation ? (a.alert.devicePosition ?? phonePositionRef.current) : null,
      at: new Date(a.alert.startedAt),
    });
    setSms('composing');
    setSms(await composeSms(contactsRef.current.map((c) => c.phone), body));
  }, []);

  const value = useMemo<KeychainContextValue>(
    () => ({
      ready,
      link,
      alert,
      alertPosition,
      sms,
      contacts,
      history,
      settings,
      log,

      discover: (onFound) => linkRef.current?.discover(onFound) ?? (() => {}),
      pair: async (d) => {
        await linkRef.current?.pair(d);
        pushLog(`vinculado ${d.name ?? d.id}`);
      },
      forget: async () => {
        await linkRef.current?.forget();
        pushLog('llavero olvidado');
      },
      setBeacon: async (on) => {
        if (settingsRef.current.haptics) void Haptics.impactAsync(Haptics.ImpactFeedbackStyle.Medium);
        await linkRef.current?.setBeacon(on).catch((e: Error) => pushLog(`baliza: ${e.message}`));
      },

      simulateAlert: (level) =>
        send({ type: 'ALERT', level, position: null, source: 'demo', at: Date.now(), id: newId() }),
      cancelAlert: () => send({ type: 'APP_CANCEL', at: Date.now() }),
      confirmNow: () => send({ type: 'CONFIRM_NOW', at: Date.now() }),
      dismissAlert: () => send({ type: 'DISMISS' }),
      sendSms,
      callEmergency: () => callNumber(settingsRef.current.emergencyNumber),

      saveContact: (c) =>
        setContacts((list) => {
          const id = c.id ?? newId();
          const item = { id, name: c.name.trim(), phone: c.phone.trim() };
          return list.some((x) => x.id === id) ? list.map((x) => (x.id === id ? item : x)) : [...list, item];
        }),
      removeContact: (id) => setContacts((list) => list.filter((x) => x.id !== id)),
      updateSettings: (patch) => setSettings((s) => ({ ...s, ...patch })),
      clearHistory: () => setHistory([]),
    }),
    [ready, link, alert, alertPosition, sms, contacts, history, settings, log, send, pushLog, sendSms],
  );

  return <KeychainContext.Provider value={value}>{children}</KeychainContext.Provider>;
}
