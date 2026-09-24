import Constants from 'expo-constants';
import { router } from 'expo-router';
import { useState } from 'react';
import { Alert, Platform, Pressable, StyleSheet, Switch, TextInput, View } from 'react-native';

import { GlassCard } from '@/components/GlassCard';
import { Icon } from '@/components/Icon';
import { IconBadge, ListRow, RowSeparator } from '@/components/ListRow';
import { Screen } from '@/components/Screen';
import { Text } from '@/components/Text';
import { useKeychain } from '@/state/KeychainProvider';
import { brand, radius, usePalette } from '@/theme';

const COUNTDOWNS = [10, 15, 20, 30];

export default function Ajustes() {
  const p = usePalette();
  const { link, settings, updateSettings, forget, simulateAlert, log } = useKeychain();
  const [message, setMessage] = useState(settings.message);
  const [showLog, setShowLog] = useState(false);

  return (
    <Screen keyboardDismissMode="interactive">
      <Section title="Llavero">
        <ListRow
          leading={
            <IconBadge color={p.primary}>
              <Icon ios="sensor.tag.radiowaves.forward.fill" android="sensors" size={16} color="#fff" />
            </IconBadge>
          }
          title={link.deviceName ?? 'Ningún llavero vinculado'}
          subtitle={link.deviceId ? `ID ${shortId(link.deviceId)}` : 'Busca llaveros cercanos'}
          chevron
          onPress={() => router.push('/vincular')}
        />
        {link.deviceId && (
          <>
            <RowSeparator />
            <ListRow
              leading={
                <IconBadge color={p.danger}>
                  <Icon ios="xmark" android="link_off" size={15} color="#fff" weight="bold" />
                </IconBadge>
              }
              title="Olvidar este llavero"
              destructive
              onPress={() =>
                Alert.alert('¿Olvidar el llavero?', 'Dejarás de recibir sus alertas hasta que lo vuelvas a vincular.', [
                  { text: 'Cancelar', style: 'cancel' },
                  { text: 'Olvidar', style: 'destructive', onPress: () => void forget() },
                ])
              }
            />
          </>
        )}
      </Section>

      <Section
        title="Alerta"
        footer="El llavero da 10 s para cancelar. La app nunca avisa antes de ese plazo; puedes darte más margen."
      >
        <Text variant="body" style={{ paddingTop: 12 }}>
          Cuenta atrás
        </Text>
        <View style={styles.segment}>
          {COUNTDOWNS.map((s) => {
            const on = settings.countdownSeconds === s;
            return (
              <Pressable
                key={s}
                onPress={() => updateSettings({ countdownSeconds: s })}
                style={[styles.segmentItem, on && { backgroundColor: p.primary }]}
                accessibilityState={{ selected: on }}
              >
                <Text variant="callout" tone={on ? 'inverse' : 'primary'} style={{ fontWeight: '600' }}>
                  {s} s
                </Text>
              </Pressable>
            );
          })}
        </View>
        <RowSeparator />
        <ListRow
          leading={
            <IconBadge color={p.success}>
              <Icon ios="location.fill" android="location_on" size={15} color="#fff" />
            </IconBadge>
          }
          title="Enviar mi ubicación"
          trailing={
            <Switch
              value={settings.shareLocation}
              onValueChange={(v) => updateSettings({ shareLocation: v })}
              trackColor={{ true: p.success }}
            />
          }
        />
        <RowSeparator />
        <ListRow
          leading={
            <IconBadge color={p.danger}>
              <Icon ios="phone.fill" android="call" size={15} color="#fff" />
            </IconBadge>
          }
          title="Emergencias"
          subtitle="Número al que llamar"
          trailing={
            <TextInput
              value={settings.emergencyNumber}
              onChangeText={(v) => updateSettings({ emergencyNumber: v.replace(/[^\d+]/g, '') })}
              keyboardType="phone-pad"
              style={[styles.inlineInput, { color: p.text }]}
              maxLength={6}
            />
          }
        />
      </Section>

      <Section title="Mensaje para tus contactos" footer="Se añade la hora y un enlace a tu ubicación.">
        <TextInput
          value={message}
          onChangeText={setMessage}
          onEndEditing={() => updateSettings({ message: message.trim() || settings.message })}
          multiline
          style={[styles.message, { color: p.text }]}
          placeholderTextColor={p.textTertiary}
          placeholder="Necesito ayuda…"
        />
      </Section>

      <Section title="Experiencia">
        <ListRow
          leading={
            <IconBadge color={brand.secondary}>
              <Icon ios="waveform" android="vibration" size={15} color="#fff" />
            </IconBadge>
          }
          title="Vibración"
          trailing={
            <Switch value={settings.haptics} onValueChange={(v) => updateSettings({ haptics: v })} trackColor={{ true: p.success }} />
          }
        />
      </Section>

      <Section title="Pruebas" footer="Simula lo que manda el llavero, sin tenerlo delante. Queda marcado como prueba.">
        <ListRow
          leading={
            <IconBadge color={p.warning}>
              <Icon ios="hand.tap.fill" android="touch_app" size={15} color="#fff" />
            </IconBadge>
          }
          title="Simular pulsación corta (ALERT:1)"
          onPress={() => simulateAlert(1)}
        />
        <RowSeparator />
        <ListRow
          leading={
            <IconBadge color={p.danger}>
              <Icon ios="timer" android="timer" size={15} color="#fff" />
            </IconBadge>
          }
          title="Simular pulsación larga (ALERT:2)"
          onPress={() => simulateAlert(2)}
        />
        <RowSeparator />
        <ListRow
          leading={
            <IconBadge color={p.textTertiary}>
              <Icon ios="info.circle" android="info" size={15} color="#fff" />
            </IconBadge>
          }
          title="Registro Bluetooth"
          subtitle={`${log.length} líneas`}
          chevron
          onPress={() => setShowLog((v) => !v)}
        />
        {showLog && (
          <View style={[styles.log, { backgroundColor: p.scheme === 'dark' ? '#000' : '#0B0D12' }]}>
            <Text variant="caption" style={styles.logText}>
              {log.length ? log.join('\n') : 'Sin actividad todavía.'}
            </Text>
          </View>
        )}
      </Section>

      <Text variant="footnote" tone="tertiary" style={{ textAlign: 'center' }}>
        {brand.name} {brand.product} · v{Constants.expoConfig?.version ?? '1.0.0'}
      </Text>
    </Screen>
  );
}

function Section({ title, footer, children }: { title: string; footer?: string; children: React.ReactNode }) {
  return (
    <View style={{ gap: 8 }}>
      <Text variant="footnote" tone="secondary" style={styles.sectionTitle}>
        {title.toUpperCase()}
      </Text>
      <GlassCard style={{ paddingVertical: 4 }}>{children}</GlassCard>
      {footer && (
        <Text variant="footnote" tone="secondary" style={styles.sectionFooter}>
          {footer}
        </Text>
      )}
    </View>
  );
}

function shortId(id: string): string {
  return id.length > 12 ? `${id.slice(0, 4)}…${id.slice(-5)}` : id;
}

const styles = StyleSheet.create({
  sectionTitle: { marginLeft: 16, letterSpacing: 0.5 },
  sectionFooter: { marginHorizontal: 16 },
  segment: { flexDirection: 'row', gap: 8, paddingVertical: 12 },
  segmentItem: {
    flex: 1,
    height: 38,
    borderRadius: radius.pill,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: 'rgba(127,127,127,0.14)',
  },
  inlineInput: { fontSize: 17, width: 64, textAlign: 'right', paddingVertical: 4 },
  message: { fontSize: 16, minHeight: 88, paddingVertical: 12, textAlignVertical: 'top' },
  log: { borderRadius: 12, padding: 12, marginBottom: 12 },
  logText: { color: '#9EF0B5', fontFamily: Platform.select({ ios: 'Menlo', default: 'monospace' }), fontWeight: '400' },
});
