import { router } from 'expo-router';
import { NativeTabs } from 'expo-router/unstable-native-tabs';
import { Pressable, StyleSheet, View } from 'react-native';

import { BrandMark } from './BrandMark';
import { Icon } from './Icon';
import { Text } from './Text';
import { describeLink } from './linkStatus';
import { useKeychain } from '@/state/KeychainProvider';
import { brand, usePalette } from '@/theme';

/**
 * Accesorio inferior de iOS 26: la pastilla de Liquid Glass que flota sobre la
 * barra de pestañas (como el mini reproductor de Música). Lleva el logo y el
 * estado del llavero, y un acceso directo a «Localizar».
 *
 * Al hacer scroll la barra se encoge y el accesorio pasa a modo `inline`,
 * junto a la pestaña activa: ahí sólo cabe el logo y una palabra.
 */
export function KeychainAccessory() {
  const placement = NativeTabs.BottomAccessory.usePlacement();
  const { link, alert, setBeacon } = useKeychain();
  const p = usePalette();
  const st = describeLink(link, p);
  const alerting = alert.phase === 'countdown';
  const inline = placement === 'inline';

  const onPress = () => {
    if (alerting) router.navigate('/alerta');
    else if (link.status === 'unpaired') router.navigate('/vincular');
    else router.navigate('/');
  };

  return (
    <Pressable onPress={onPress} style={[styles.row, inline && styles.inline]}>
      <BrandMark size={inline ? 22 : 30} pulse={alerting} />
      <View style={styles.texts}>
        <Text variant={inline ? 'footnote' : 'callout'} style={{ fontWeight: '600' }} numberOfLines={1}>
          {alerting ? 'Alerta en curso' : st.label}
        </Text>
        {!inline && (
          <View style={styles.statusLine}>
            <View style={[styles.dot, { backgroundColor: alerting ? p.danger : st.color }]} />
            <Text variant="caption" tone="secondary" numberOfLines={1}>
              {link.deviceName ?? brand.name}
            </Text>
          </View>
        )}
      </View>
      {!inline && link.status === 'connected' && (
        <Pressable hitSlop={12} onPress={() => void setBeacon(!link.beacon)} accessibilityLabel="Localizar llavero">
          <Icon
            ios="speaker.wave.3.fill"
            android="volume_up"
            size={20}
            color={link.beacon ? p.accent : p.textSecondary}
          />
        </Pressable>
      )}
    </Pressable>
  );
}

const styles = StyleSheet.create({
  row: { flex: 1, flexDirection: 'row', alignItems: 'center', gap: 12, paddingHorizontal: 14 },
  inline: { gap: 8, paddingHorizontal: 10 },
  texts: { flex: 1 },
  statusLine: { flexDirection: 'row', alignItems: 'center', gap: 6 },
  dot: { width: 7, height: 7, borderRadius: 4 },
});
