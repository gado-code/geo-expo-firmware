import { router } from 'expo-router';
import { StyleSheet, View } from 'react-native';

import { Button } from '@/components/Button';
import { GlassCard } from '@/components/GlassCard';
import { Icon } from '@/components/Icon';
import { ListRow, RowSeparator } from '@/components/ListRow';
import { Screen } from '@/components/Screen';
import { Text } from '@/components/Text';
import { useKeychain } from '@/state/KeychainProvider';
import { usePalette } from '@/theme';

export default function Contactos() {
  const p = usePalette();
  const { contacts } = useKeychain();

  return (
    <Screen>
      {contacts.length === 0 ? (
        <GlassCard style={styles.empty} tint={p.scheme === 'dark' ? 'rgba(255,59,48,0.12)' : 'rgba(255,59,48,0.08)'}>
          <Icon ios="person.2.fill" android="group" size={40} color={p.danger} />
          <Text variant="title">Nadie recibirá tu alerta</Text>
          <Text variant="callout" tone="secondary" style={{ textAlign: 'center' }}>
            Añade al menos una persona de confianza. Cuando se confirme una alerta le enviaremos tu ubicación por SMS.
          </Text>
        </GlassCard>
      ) : (
        <GlassCard style={{ paddingVertical: 4 }}>
          {contacts.map((c, i) => (
            <View key={c.id}>
              {i > 0 && <RowSeparator />}
              <ListRow
                leading={<Avatar name={c.name} color={p.pastel} ink={p.onPrimary} />}
                title={c.name}
                subtitle={c.phone}
                chevron
                onPress={() => router.push({ pathname: '/contacto', params: { id: c.id } })}
              />
            </View>
          ))}
        </GlassCard>
      )}

      <Button
        title="Añadir contacto"
        icon={<Icon ios="plus" android="add" size={18} color={p.onPrimary} weight="bold" />}
        onPress={() => router.push('/contacto')}
      />

      <Text variant="footnote" tone="secondary" style={{ textAlign: 'center', paddingHorizontal: 12 }}>
        En iPhone, el sistema te pedirá pulsar «Enviar» en el mensaje ya redactado: iOS no permite a ninguna app
        enviar SMS por su cuenta.
      </Text>
    </Screen>
  );
}

function Avatar({ name, color, ink }: { name: string; color: string; ink: string }) {
  const initials = name
    .split(/\s+/)
    .filter(Boolean)
    .slice(0, 2)
    .map((w) => w[0]!.toUpperCase())
    .join('');
  return (
    <View style={[styles.avatar, { backgroundColor: color }]}>
      <Text variant="callout" style={{ fontWeight: '700', color: ink }}>
        {initials || '?'}
      </Text>
    </View>
  );
}

const styles = StyleSheet.create({
  empty: { alignItems: 'center', gap: 10, paddingVertical: 32 },
  avatar: { width: 40, height: 40, borderRadius: 20, alignItems: 'center', justifyContent: 'center' },
});
