/**
 * Lo que la app hace con una alerta: avisos locales, ubicación y SMS.
 *
 * Nota sobre iOS: el sistema no deja enviar SMS sin que el usuario pulse
 * «Enviar». Por eso al confirmarse la alerta se abre el compositor ya relleno.
 * Para avisar a los contactos sin intervención hace falta un servidor (p. ej.
 * un proveedor de SMS); queda preparado en `notifyContacts`.
 */
import * as Location from 'expo-location';
import * as Notifications from 'expo-notifications';
import * as SMS from 'expo-sms';
import { Linking, Platform } from 'react-native';

import type { Position } from '@/protocol/nus.ts';

const CHANNEL = 'alertas';

export async function setupNotifications(): Promise<void> {
  Notifications.setNotificationHandler({
    // Con la app delante ya se ve la pantalla de alerta: sin banner duplicado.
    handleNotification: async () => ({
      shouldShowBanner: false,
      shouldShowList: true,
      shouldPlaySound: false,
      shouldSetBadge: false,
    }),
  });
  if (Platform.OS === 'android') {
    await Notifications.setNotificationChannelAsync(CHANNEL, {
      name: 'Alertas del llavero',
      importance: Notifications.AndroidImportance.MAX,
      vibrationPattern: [0, 400, 200, 400, 200, 800],
      lockscreenVisibility: Notifications.AndroidNotificationVisibility.PUBLIC,
      bypassDnd: true,
    }).catch(() => {});
  }
  await Notifications.requestPermissionsAsync().catch(() => {});
}

/**
 * Programa los avisos locales de una alerta. Se programan en el sistema (no
 * con temporizadores JS) para que salten aunque la app esté en segundo plano
 * y el sistema haya congelado el JavaScript. Devuelve los identificadores
 * para poder anularlos si llega CANCEL.
 */
export async function scheduleAlertNotifications(deadline: number, level: 1 | 2): Promise<string[]> {
  const secondsLeft = Math.max(1, Math.round((deadline - Date.now()) / 1000));
  const ids: string[] = [];
  try {
    ids.push(
      await Notifications.scheduleNotificationAsync({
        content: {
          title: level === 2 ? 'Alerta prolongada del llavero' : 'Alerta del llavero',
          body: `Se avisará a tus contactos en ${secondsLeft} s. Pulsa el llavero otra vez para cancelar.`,
          sound: true,
          interruptionLevel: 'timeSensitive',
          priority: Notifications.AndroidNotificationPriority.MAX,
        },
        trigger: Platform.OS === 'android' ? { channelId: CHANNEL } : null,
      }),
    );
    ids.push(
      await Notifications.scheduleNotificationAsync({
        content: {
          title: 'Alerta confirmada',
          body: 'Toca para enviar tu ubicación a tus contactos de emergencia.',
          sound: true,
          interruptionLevel: 'timeSensitive',
          priority: Notifications.AndroidNotificationPriority.MAX,
          data: { route: '/alerta' },
        },
        trigger: {
          type: Notifications.SchedulableTriggerInputTypes.TIME_INTERVAL,
          seconds: secondsLeft,
          channelId: CHANNEL,
        },
      }),
    );
  } catch {
    // Sin permiso de notificaciones: la pantalla de alerta sigue funcionando.
  }
  return ids;
}

export async function cancelNotifications(ids: string[]): Promise<void> {
  await Promise.all(ids.map((id) => Notifications.cancelScheduledNotificationAsync(id).catch(() => {})));
  await Notifications.dismissAllNotificationsAsync().catch(() => {});
}

export async function currentPosition(): Promise<Position | null> {
  try {
    const perm = await Location.requestForegroundPermissionsAsync();
    if (perm.status !== 'granted') return null;
    const last = await Location.getLastKnownPositionAsync({ maxAge: 60_000 });
    const fix =
      last ??
      (await Promise.race([
        Location.getCurrentPositionAsync({ accuracy: Location.Accuracy.High }),
        new Promise<null>((r) => setTimeout(() => r(null), 8000)),
      ]));
    return fix ? { lat: fix.coords.latitude, lon: fix.coords.longitude } : null;
  } catch {
    return null;
  }
}

export async function composeSms(phones: string[], body: string): Promise<'sent' | 'cancelled' | 'unavailable'> {
  if (phones.length === 0 || !(await SMS.isAvailableAsync().catch(() => false))) return 'unavailable';
  try {
    const { result } = await SMS.sendSMSAsync(phones, body);
    return result === 'cancelled' ? 'cancelled' : 'sent';
  } catch {
    return 'unavailable';
  }
}

export function callNumber(number: string): void {
  void Linking.openURL(`tel:${number.replace(/[^\d+]/g, '')}`).catch(() => {});
}
