import { router } from 'expo-router';
import { useEffect, useState } from 'react';
import { ActivityIndicator, ScrollView, StyleSheet, View } from 'react-native';

import { BrandMark } from '@/components/BrandMark';
import { GlassCard } from '@/components/GlassCard';
import { ListRow, RowSeparator } from '@/components/ListRow';
import { Text } from '@/components/Text';
import { describeLink } from '@/components/linkStatus';
import type { FoundDevice } from '@/ble/KeychainLink';
import { useKeychain } from '@/state/KeychainProvider';
import { usePalette } from '@/theme';

/**
 * Hoja de vinculación. Lista los llaveros que anuncian el servicio NUS,
 * ordenados por cercanía. No hay PIN ni emparejamiento del sistema (§2):
 * elegir uno aquí basta.
 */
export default function Vincular() {
  const p = usePalette();
  const { link, discover, pair } = useKeychain();
  const [found, setFound] = useState<Record<string, FoundDevice & { seen: number }>>({});
  const canScan = !['unavailable', 'unauthorized', 'poweredOff'].includes(link.status);

  useEffect(() => {
    if (!canScan) return;
    const stop = discover((d) => setFound((f) => ({ ...f, [d.id]: { ...d, seen: Date.now() } })));
    // Quitar los que dejan de anunciarse.
    const prune = setInterval(
      () =>
        setFound((f) => Object.fromEntries(Object.entries(f).filter(([, d]) => Date.now() - d.seen < 8000))),
      2000,
    );
    return () => {
      clearInterval(prune);
      stop();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [canScan]);

  const list = Object.values(found).sort((a, b) => (b.rssi ?? -127) - (a.rssi ?? -127));

  return (
    <ScrollView contentContainerStyle={styles.content} style={{ backgroundColor: p.backgroundElevated }}>
      <View style={styles.header}>
        <BrandMark size={56} />
        <Text variant="title">Vincular llavero</Text>
        <Text variant="callout" tone="secondary" style={{ textAlign: 'center' }}>
          Acerca el llavero al teléfono. No hace falta emparejarlo desde los ajustes de Bluetooth: mejor no lo hagas.
        </Text>
      </View>

      {!canScan ? (
        <GlassCard>
          <Text variant="headline">{describeLink(link, p).label}</Text>
          <Text variant="callout" tone="secondary">
            {describeLink(link, p).detail}
          </Text>
        </GlassCard>
      ) : (
        <GlassCard style={{ paddingVertical: 4 }}>
          {list.length === 0 ? (
            <View style={styles.searching}>
              <ActivityIndicator />
              <Text variant="callout" tone="secondary">
                Buscando llaveros cercanos…
              </Text>
            </View>
          ) : (
            list.map((d, i) => (
              <View key={d.id}>
                {i > 0 && <RowSeparator />}
                <ListRow
                  leading={<BrandMark size={32} />}
                  title={d.name ?? 'Llavero Ivy'}
                  subtitle={`${proximity(d.rssi)}${d.id === link.deviceId ? ' · vinculado' : ''}`}
                  chevron
                  onPress={async () => {
                    await pair(d);
                    router.back();
                  }}
                />
              </View>
            ))
          )}
        </GlassCard>
      )}
    </ScrollView>
  );
}

function proximity(rssi: number | null): string {
  if (rssi == null) return 'Cerca';
  if (rssi > -55) return 'Muy cerca';
  if (rssi > -70) return 'Cerca';
  return 'Lejos';
}

const styles = StyleSheet.create({
  content: { padding: 20, paddingTop: 28, gap: 20 },
  header: { alignItems: 'center', gap: 10 },
  searching: { flexDirection: 'row', gap: 12, alignItems: 'center', paddingVertical: 18, justifyContent: 'center' },
});
