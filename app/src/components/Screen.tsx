import { LinearGradient } from 'expo-linear-gradient';
import type { ReactNode } from 'react';
import { Platform, ScrollView, StyleSheet, View, type ScrollViewProps } from 'react-native';

import { space, usePalette } from '@/theme';

/**
 * Pantalla con scroll y un degradado de fondo suave: el vidrio necesita algo
 * detrás para verse. En iOS el primer ScrollView ajusta solo sus márgenes a la
 * cabecera grande y a la barra de pestañas de Liquid Glass.
 */
export function Screen({ children, ...rest }: { children: ReactNode } & ScrollViewProps) {
  const p = usePalette();
  return (
    <View style={[styles.root, { backgroundColor: p.background }]}>
      <LinearGradient colors={p.ambient} style={StyleSheet.absoluteFill} start={{ x: 0, y: 0 }} end={{ x: 1, y: 1 }} />
      <ScrollView
        contentInsetAdjustmentBehavior="automatic"
        contentContainerStyle={styles.content}
        showsVerticalScrollIndicator={false}
        {...rest}
      >
        {children}
      </ScrollView>
    </View>
  );
}

const styles = StyleSheet.create({
  root: { flex: 1 },
  content: {
    padding: space.lg,
    paddingTop: Platform.OS === 'android' ? space.lg : space.sm,
    paddingBottom: 140,
    gap: space.lg,
  },
});
