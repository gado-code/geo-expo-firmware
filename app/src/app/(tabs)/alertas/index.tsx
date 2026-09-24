import { Alert, Linking, StyleSheet, View } from 'react-native';

import { Button } from '@/components/Button';
import { GlassCard } from '@/components/GlassCard';
import { Icon } from '@/components/Icon';
import { IconBadge, ListRow, RowSeparator } from '@/components/ListRow';
import { Screen } from '@/components/Screen';
import { Text } from '@/components/Text';
import { mapsLink } from '@/services/message.ts';
import { useKeychain } from '@/state/KeychainProvider';
import type { HistoryEntry } from '@/state/storage';
import { usePalette, type Palette } from '@/theme';

export default function Alertas() {
  const p = usePalette();
  const { history, clearHistory } = useKeychain();

  if (history.length === 0) {
    return (
      <Screen>
        <GlassCard style={styles.empty}>
          <Icon ios="checkmark.circle.fill" android="check_circle" size={44} color={p.success} />
          <Text variant="title">Sin alertas</Text>
          <Text variant="callout" tone="secondary" style={{ textAlign: 'center' }}>
            Aquí verás cada vez que se active el llavero, si se canceló o se confirmó, y dónde estabas.
          </Text>
        </GlassCard>
      </Screen>
    );
  }

  const groups = groupByDay(history);

  return (
    <Screen>
      {groups.map(([day, items]) => (
        <View key={day} style={{ gap: 8 }}>
          <Text variant="footnote" tone="secondary" style={styles.section}>
            {day.toUpperCase()}
          </Text>
          <GlassCard style={{ paddingVertical: 4 }}>
            {items.map((h, i) => (
              <View key={h.id}>
                {i > 0 && <RowSeparator />}
                <HistoryRow entry={h} p={p} />
              </View>
            ))}
          </GlassCard>
        </View>
      ))}
      <Button
        kind="plain"
        title="Borrar historial"
        onPress={() =>
          Alert.alert('¿Borrar el historial?', 'No se puede deshacer.', [
            { text: 'Cancelar', style: 'cancel' },
            { text: 'Borrar', style: 'destructive', onPress: clearHistory },
          ])
        }
      />
    </Screen>
  );
}

function HistoryRow({ entry: h, p }: { entry: HistoryEntry; p: Palette }) {
  const confirmed = h.outcome === 'confirmed';
  const time = new Date(h.startedAt).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  const how =
    h.outcome === 'confirmed' ? 'Confirmada' : h.outcome === 'cancelled-device' ? 'Cancelada en el llavero' : 'Cancelada en la app';
  return (
    <ListRow
      leading={
        <IconBadge color={confirmed ? p.danger : p.success}>
          {confirmed ? (
            <Icon ios="exclamationmark.triangle.fill" android="warning" size={16} color="#fff" />
          ) : (
            <Icon ios="xmark.circle.fill" android="cancel" size={16} color="#fff" />
          )}
        </IconBadge>
      }
      title={`${h.level === 2 ? 'Alerta prolongada' : 'Alerta'}${h.source === 'demo' ? ' (prueba)' : ''}`}
      subtitle={`${how} · ${time}`}
      trailing={
        h.position ? <Icon ios="map.fill" android="map" size={18} color={p.primary} /> : undefined
      }
      onPress={h.position ? () => void Linking.openURL(mapsLink(h.position!)) : undefined}
    />
  );
}

function groupByDay(list: HistoryEntry[]): [string, HistoryEntry[]][] {
  const today = new Date().toDateString();
  const yesterday = new Date(Date.now() - 86_400_000).toDateString();
  const map = new Map<string, HistoryEntry[]>();
  for (const h of list) {
    const d = new Date(h.startedAt);
    const key =
      d.toDateString() === today
        ? 'Hoy'
        : d.toDateString() === yesterday
          ? 'Ayer'
          : d.toLocaleDateString([], { weekday: 'long', day: 'numeric', month: 'long' });
    map.set(key, [...(map.get(key) ?? []), h]);
  }
  return [...map.entries()];
}

const styles = StyleSheet.create({
  empty: { alignItems: 'center', gap: 10, paddingVertical: 40 },
  section: { marginLeft: 16, letterSpacing: 0.5 },
});
