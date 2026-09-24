import { router, useLocalSearchParams } from 'expo-router';
import { useState } from 'react';
import { Alert, ScrollView, StyleSheet, TextInput, View } from 'react-native';

import { Button } from '@/components/Button';
import { GlassCard } from '@/components/GlassCard';
import { RowSeparator } from '@/components/ListRow';
import { Text } from '@/components/Text';
import { useKeychain } from '@/state/KeychainProvider';
import { usePalette } from '@/theme';

export default function Contacto() {
  const p = usePalette();
  const { id } = useLocalSearchParams<{ id?: string }>();
  const { contacts, saveContact, removeContact } = useKeychain();
  const existing = contacts.find((c) => c.id === id);
  const [name, setName] = useState(existing?.name ?? '');
  const [phone, setPhone] = useState(existing?.phone ?? '');

  const valid = name.trim().length > 0 && phone.replace(/\D/g, '').length >= 7;

  const input = [styles.input, { color: p.text }];

  return (
    <ScrollView
      contentContainerStyle={styles.content}
      style={{ backgroundColor: p.backgroundElevated }}
      keyboardShouldPersistTaps="handled"
    >
      <Text variant="title" style={{ textAlign: 'center' }}>
        {existing ? 'Editar contacto' : 'Nuevo contacto'}
      </Text>
      <GlassCard style={{ paddingVertical: 4 }}>
        <TextInput
          value={name}
          onChangeText={setName}
          placeholder="Nombre"
          placeholderTextColor={p.textTertiary}
          style={input}
          autoFocus={!existing}
          textContentType="name"
          autoComplete="name"
          returnKeyType="next"
        />
        <RowSeparator />
        <TextInput
          value={phone}
          onChangeText={setPhone}
          placeholder="Teléfono (con indicativo, p. ej. +57…)"
          placeholderTextColor={p.textTertiary}
          style={input}
          keyboardType="phone-pad"
          textContentType="telephoneNumber"
          autoComplete="tel"
        />
      </GlassCard>
      <Button
        title="Guardar"
        disabled={!valid}
        onPress={() => {
          saveContact({ id: existing?.id, name, phone });
          router.back();
        }}
      />
      {existing && (
        <View>
          <Button
            kind="plain"
            title="Eliminar contacto"
            onPress={() =>
              Alert.alert(`¿Eliminar a ${existing.name}?`, 'Dejará de recibir tus alertas.', [
                { text: 'Cancelar', style: 'cancel' },
                {
                  text: 'Eliminar',
                  style: 'destructive',
                  onPress: () => {
                    removeContact(existing.id);
                    router.back();
                  },
                },
              ])
            }
          />
        </View>
      )}
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  content: { padding: 20, paddingTop: 28, gap: 18 },
  input: { fontSize: 17, paddingVertical: 14, paddingLeft: 4 },
});
