import { router } from 'expo-router';
import { useEffect, useState } from 'react';
import { Animated, Easing, Pressable, StyleSheet, View } from 'react-native';

import { BrandMark } from '@/components/BrandMark';
import { Button } from '@/components/Button';
import { GlassCard } from '@/components/GlassCard';
import { Icon } from '@/components/Icon';
import { describeLink } from '@/components/linkStatus';
import { Screen } from '@/components/Screen';
import { Text } from '@/components/Text';
import { useKeychain } from '@/state/KeychainProvider';
import { brand, usePalette } from '@/theme';

export default function Inicio() {
  const p = usePalette();
  const { link, contacts, history, setBeacon, simulateAlert } = useKeychain();
  const st = describeLink(link, p);
  const connected = link.status === 'connected';
  const last = history[0];

  return (
    <Screen>
      {/* Tarjeta principal: el llavero y su estado */}
      <GlassCard style={styles.hero}>
        <PulseRing color={st.color} active={st.live}>
          <BrandMark size={84} />
        </PulseRing>
        <View style={styles.heroTexts}>
          <View style={styles.statusLine}>
            <View style={[styles.dot, { backgroundColor: st.color }]} />
            <Text variant="headline" style={{ color: st.color }}>
              {st.label}
            </Text>
          </View>
          <Text variant="title" style={{ textAlign: 'center' }}>
            {link.deviceName ?? brand.name}
          </Text>
          <Text variant="callout" tone="secondary" style={{ textAlign: 'center' }}>
            {st.detail}
          </Text>
          {connected && link.rssi != null && <SignalBars rssi={link.rssi} />}
        </View>
        {link.status === 'unpaired' && (
          <Button
            title="Vincular llavero"
            icon={<Icon ios="link.badge.plus" android="add" color={p.onPrimary} size={18} />}
            onPress={() => router.push('/vincular')}
          />
        )}
      </GlassCard>

      {/* Acciones rápidas */}
      <View style={styles.grid}>
        <QuickTile
          title={link.beacon ? 'Sonando' : 'Localizar'}
          subtitle={connected ? (link.beacon ? 'Toca para parar' : 'Haz sonar el llavero') : 'Conecta el llavero'}
          icon={<Icon ios="speaker.wave.3.fill" android="volume_up" color={p.onPrimary} size={20} />}
          color={link.beacon ? p.warning : p.primary}
          disabled={!connected}
          onPress={() => void setBeacon(!link.beacon)}
        />
        <QuickTile
          title={`${contacts.length} contacto${contacts.length === 1 ? '' : 's'}`}
          subtitle={contacts.length ? 'Recibirán tu alerta' : 'Añade a quién avisar'}
          icon={<Icon ios="person.2.fill" android="group" color={contacts.length ? p.onPrimary : '#fff'} size={20} />}
          color={contacts.length ? p.pastel : p.danger}
          onPress={() => router.navigate('/contactos')}
        />
      </View>

      {/* Cómo se usa el botón */}
      <GlassCard>
        <Text variant="headline" style={{ marginBottom: 12 }}>
          Cómo usar el botón
        </Text>
        <Step n="1" title="Pulsa una vez" text="Empieza una alerta con 10 s para arrepentirte." color={p.primary} ink={p.onPrimary} />
        <Step n="2" title="Mantén 3 segundos" text="Alerta prolongada: la situación es más grave." color={p.danger} />
        <Step n="↺" title="Pulsa otra vez antes de 10 s" text="Cancela la alerta: falsa alarma." color={p.success} last />
      </GlassCard>

      {last && (
        <GlassCard>
          <Text variant="caption" tone="secondary" style={styles.eyebrow}>
            ÚLTIMA ALERTA
          </Text>
          <Text variant="headline">
            {last.outcome === 'confirmed' ? 'Confirmada' : 'Cancelada'} · nivel {last.level}
          </Text>
          <Text variant="footnote" tone="secondary">
            {new Date(last.startedAt).toLocaleString()}
            {last.source === 'demo' ? ' · prueba' : ''}
          </Text>
        </GlassCard>
      )}

      <Button
        kind="plain"
        title="Probar una alerta (modo demo)"
        icon={<Icon ios="hand.tap.fill" android="touch_app" color={p.accent} size={18} />}
        onPress={() => simulateAlert(1)}
      />
    </Screen>
  );
}

function QuickTile(props: {
  title: string;
  subtitle: string;
  icon: React.ReactNode;
  color: string;
  onPress: () => void;
  disabled?: boolean;
}) {
  return (
    <Pressable
      style={styles.tile}
      onPress={props.onPress}
      disabled={props.disabled}
      accessibilityRole="button"
      accessibilityLabel={props.title}
    >
      <GlassCard style={[styles.tileCard, props.disabled && { opacity: 0.55 }]} interactive>
        <View style={[styles.tileIcon, { backgroundColor: props.color }]}>{props.icon}</View>
        <Text variant="headline">{props.title}</Text>
        <Text variant="footnote" tone="secondary">
          {props.subtitle}
        </Text>
      </GlassCard>
    </Pressable>
  );
}

function Step({
  n,
  title,
  text,
  color,
  ink = '#fff',
  last,
}: {
  n: string;
  title: string;
  text: string;
  color: string;
  ink?: string;
  last?: boolean;
}) {
  return (
    <View style={[styles.step, !last && { marginBottom: 14 }]}>
      <View style={[styles.stepNum, { backgroundColor: color }]}>
        <Text variant="callout" style={{ fontWeight: '700', color: ink }}>
          {n}
        </Text>
      </View>
      <View style={{ flex: 1 }}>
        <Text variant="callout" style={{ fontWeight: '600' }}>
          {title}
        </Text>
        <Text variant="footnote" tone="secondary">
          {text}
        </Text>
      </View>
    </View>
  );
}

function SignalBars({ rssi }: { rssi: number }) {
  const p = usePalette();
  const level = rssi > -60 ? 4 : rssi > -70 ? 3 : rssi > -80 ? 2 : 1;
  return (
    <View style={styles.bars} accessibilityLabel={`Señal ${level} de 4`}>
      {[1, 2, 3, 4].map((i) => (
        <View
          key={i}
          style={{
            width: 5,
            height: 5 + i * 4,
            borderRadius: 2,
            backgroundColor: i <= level ? p.success : p.separator,
          }}
        />
      ))}
      <Text variant="caption" tone="tertiary" style={{ marginLeft: 6 }}>
        {rssi} dBm
      </Text>
    </View>
  );
}

/** Anillo que late mientras el llavero está conectado. */
function PulseRing({ children, color, active }: { children: React.ReactNode; color: string; active: boolean }) {
  const [v] = useState(() => new Animated.Value(0));
  useEffect(() => {
    if (!active) {
      v.stopAnimation();
      v.setValue(0);
      return;
    }
    const loop = Animated.loop(
      Animated.timing(v, { toValue: 1, duration: 2200, easing: Easing.out(Easing.quad), useNativeDriver: true }),
    );
    loop.start();
    return () => loop.stop();
  }, [active, v]);

  return (
    <View style={styles.pulseBox}>
      {active && (
        <Animated.View
          style={[
            styles.pulse,
            {
              borderColor: color,
              opacity: v.interpolate({ inputRange: [0, 1], outputRange: [0.6, 0] }),
              transform: [{ scale: v.interpolate({ inputRange: [0, 1], outputRange: [0.8, 1.5] }) }],
            },
          ]}
        />
      )}
      <View style={[styles.pulseStatic, { borderColor: color }]} />
      {children}
    </View>
  );
}

const styles = StyleSheet.create({
  hero: { alignItems: 'center', gap: 18, paddingVertical: 28 },
  heroTexts: { alignItems: 'center', gap: 6 },
  statusLine: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  dot: { width: 9, height: 9, borderRadius: 5 },
  grid: { flexDirection: 'row', gap: 12 },
  tile: { flex: 1 },
  tileCard: { flex: 1, gap: 4 },
  tileIcon: {
    width: 38,
    height: 38,
    borderRadius: 11,
    borderCurve: 'continuous',
    alignItems: 'center',
    justifyContent: 'center',
    marginBottom: 8,
  },
  step: { flexDirection: 'row', gap: 12, alignItems: 'flex-start' },
  stepNum: { width: 28, height: 28, borderRadius: 14, alignItems: 'center', justifyContent: 'center' },
  eyebrow: { letterSpacing: 0.8, marginBottom: 4 },
  bars: { flexDirection: 'row', alignItems: 'flex-end', gap: 3, marginTop: 6 },
  pulseBox: { width: 132, height: 132, alignItems: 'center', justifyContent: 'center' },
  pulse: { position: 'absolute', width: 132, height: 132, borderRadius: 66, borderWidth: 2 },
  pulseStatic: { position: 'absolute', width: 116, height: 116, borderRadius: 58, borderWidth: 1.5, opacity: 0.35 },
});
