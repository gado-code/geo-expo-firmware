import { LinearGradient } from 'expo-linear-gradient';
import { router } from 'expo-router';
import { useEffect, useRef, useState } from 'react';
import { AppState, StyleSheet, View } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { BrandMark } from '@/components/BrandMark';
import { Button } from '@/components/Button';
import { GlassCard } from '@/components/GlassCard';
import { Icon } from '@/components/Icon';
import { Text } from '@/components/Text';
import { effectiveCountdownMs, remainingMs } from '@/protocol/alertMachine.ts';
import { useKeychain } from '@/state/KeychainProvider';
import { brand } from '@/theme';

/**
 * Pantalla completa de alerta. Tres fases:
 *  - cuenta atrás: se puede cancelar (aquí o pulsando el llavero) o avisar ya;
 *  - confirmada: se abre el SMS a los contactos con la ubicación;
 *  - cancelada: confirmación tranquila y cerrar.
 */
export default function AlertaScreen() {
  const insets = useSafeAreaInsets();
  const { alert, alertPosition, sms, contacts, settings, cancelAlert, confirmNow, dismissAlert, sendSms, callEmergency } =
    useKeychain();
  const [now, setNow] = useState(() => Date.now());
  const autoSent = useRef<string | null>(null);

  useEffect(() => {
    if (alert.phase !== 'countdown') return;
    const t = setInterval(() => setNow(Date.now()), 100);
    return () => clearInterval(t);
  }, [alert.phase]);

  // Al confirmarse con la app delante, abrimos el SMS una sola vez.
  useEffect(() => {
    if (alert.phase !== 'confirmed' || autoSent.current === alert.alert.id) return;
    if (contacts.length === 0 || AppState.currentState !== 'active') return;
    autoSent.current = alert.alert.id;
    // Pequeño margen para que llegue la ubicación del teléfono.
    const t = setTimeout(() => void sendSms(), alertPosition ? 0 : 1500);
    return () => clearTimeout(t);
  }, [alert, contacts.length, sendSms, alertPosition]);

  // Única salida de la pantalla: cuando la alerta vuelve a reposo (al pulsar
  // «Cerrar», o si se abrió desde una notificación antigua sin alerta).
  const close = dismissAlert;
  useEffect(() => {
    if (alert.phase !== 'idle') return;
    if (router.canGoBack()) router.back();
    else router.replace('/');
  }, [alert.phase]);

  if (alert.phase === 'idle') return <View style={{ flex: 1, backgroundColor: brand.dangerDeep }} />;

  const level = alert.alert.level;
  const total = effectiveCountdownMs(settings.countdownSeconds);
  const left = remainingMs(alert, now);
  const secs = Math.ceil(left / 1000);
  const progress = alert.phase === 'countdown' ? left / Math.max(total, alert.alert.deadline - alert.alert.startedAt) : 0;

  const colors: [string, string] =
    alert.phase === 'cancelled' ? ['#0E3B2A', '#07140F'] : [brand.danger, brand.dangerDeep];

  return (
    <View style={styles.root}>
      <LinearGradient colors={colors} style={StyleSheet.absoluteFill} />
      <View style={[styles.content, { paddingTop: insets.top + 24, paddingBottom: insets.bottom + 24 }]}>
        <View style={styles.header}>
          <BrandMark size={34} />
          <Text variant="headline" tone="inverse">
            {alert.alert.source === 'demo' ? 'Prueba de alerta' : 'Alerta del llavero'}
          </Text>
        </View>

        {alert.phase === 'countdown' && (
          <>
            <View style={styles.center}>
              <Text variant="title" tone="inverse" style={styles.kicker}>
                {level === 2 ? 'ALERTA PROLONGADA' : 'ALERTA ACTIVADA'}
              </Text>
              <Text tone="inverse" style={styles.count} accessibilityLabel={`${secs} segundos`}>
                {secs}
              </Text>
              <View style={styles.track}>
                <View style={[styles.bar, { width: `${Math.max(0, Math.min(1, progress)) * 100}%` }]} />
              </View>
              <Text variant="body" tone="inverse" style={styles.explain}>
                {contacts.length
                  ? `Avisaremos a ${contacts.length} contacto${contacts.length === 1 ? '' : 's'} al terminar.`
                  : 'No tienes contactos: podrás llamar a emergencias.'}
                {'\n'}Pulsa el llavero otra vez para cancelar.
              </Text>
            </View>
            <View style={styles.actions}>
              <Button onDark kind="glass" large title="Estoy bien, cancelar" onPress={cancelAlert} />
              <Button onDark kind="plain" title="Avisar ya" onPress={confirmNow} />
            </View>
          </>
        )}

        {alert.phase === 'confirmed' && (
          <>
            <View style={styles.center}>
              <Icon ios="sos" android="sos" size={72} color="#fff" />
              <Text variant="hero" tone="inverse" style={{ textAlign: 'center' }}>
                Alerta confirmada
              </Text>
              <GlassCard style={styles.info} tint="rgba(255,255,255,0.12)">
                <InfoLine
                  icon={<Icon ios="location.fill" android="location_on" size={16} color="#fff" />}
                  text={
                    alertPosition
                      ? `${alertPosition.lat.toFixed(5)}, ${alertPosition.lon.toFixed(5)}`
                      : settings.shareLocation
                        ? 'Obteniendo ubicación…'
                        : 'Ubicación desactivada'
                  }
                />
                <InfoLine
                  icon={<Icon ios="message.fill" android="sms" size={16} color="#fff" />}
                  text={smsText(sms, contacts.length)}
                />
              </GlassCard>
            </View>
            <View style={styles.actions}>
              {contacts.length > 0 && (
                <Button
                  onDark
                  kind="glass"
                  large
                  title={sms === 'sent' ? 'Reenviar SMS' : 'Enviar SMS a contactos'}
                  icon={<Icon ios="message.fill" android="sms" size={18} color="#fff" />}
                  loading={sms === 'composing'}
                  onPress={() => void sendSms()}
                />
              )}
              <Button
                onDark
                kind="glass"
                large
                title={`Llamar al ${settings.emergencyNumber}`}
                icon={<Icon ios="phone.fill" android="call" size={18} color="#fff" />}
                onPress={callEmergency}
              />
              <Button onDark kind="plain" title="Cerrar" onPress={close} />
            </View>
          </>
        )}

        {alert.phase === 'cancelled' && (
          <>
            <View style={styles.center}>
              <Icon ios="checkmark.circle.fill" android="check_circle" size={84} color={brand.success} />
              <Text variant="hero" tone="inverse" style={{ textAlign: 'center' }}>
                Alerta cancelada
              </Text>
              <Text variant="body" tone="inverse" style={styles.explain}>
                {alert.by === 'device' ? 'La cancelaste desde el llavero.' : 'La cancelaste desde la app.'} No se ha
                avisado a nadie.
              </Text>
            </View>
            <View style={styles.actions}>
              <Button onDark kind="glass" large title="Cerrar" onPress={close} />
            </View>
          </>
        )}
      </View>
    </View>
  );
}

function InfoLine({ icon, text }: { icon: React.ReactNode; text: string }) {
  return (
    <View style={styles.infoLine}>
      {icon}
      <Text variant="callout" tone="inverse" style={{ flex: 1 }}>
        {text}
      </Text>
    </View>
  );
}

function smsText(s: string, n: number): string {
  if (n === 0) return 'No hay contactos de emergencia';
  switch (s) {
    case 'composing':
      return 'Preparando el mensaje…';
    case 'sent':
      return 'Mensaje enviado';
    case 'cancelled':
      return 'Mensaje sin enviar';
    case 'unavailable':
      return 'Este dispositivo no puede enviar SMS';
    default:
      return `Preparando aviso a ${n} contacto${n === 1 ? '' : 's'}`;
  }
}

const styles = StyleSheet.create({
  root: { flex: 1, backgroundColor: brand.dangerDeep },
  content: { flex: 1, paddingHorizontal: 24, justifyContent: 'space-between' },
  header: { flexDirection: 'row', alignItems: 'center', gap: 12, alignSelf: 'center' },
  center: { alignItems: 'center', gap: 16 },
  kicker: { letterSpacing: 2, opacity: 0.9, fontSize: 17 },
  count: { fontSize: 140, lineHeight: 150, fontWeight: '800', fontVariant: ['tabular-nums'] },
  track: { width: '80%', height: 8, borderRadius: 4, backgroundColor: 'rgba(255,255,255,0.25)', overflow: 'hidden' },
  bar: { height: 8, borderRadius: 4, backgroundColor: '#fff' },
  explain: { textAlign: 'center', opacity: 0.9 },
  actions: { gap: 12 },
  info: { alignSelf: 'stretch', gap: 12 },
  infoLine: { flexDirection: 'row', alignItems: 'center', gap: 10 },
});
