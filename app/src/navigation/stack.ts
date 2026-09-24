import type { ComponentProps } from 'react';
import { Platform } from 'react-native';
import type { Stack } from 'expo-router';

import type { Palette } from '@/theme';

type Options = NonNullable<ComponentProps<typeof Stack>['screenOptions']>;

/**
 * Cabecera de título grande. En iOS 26 es transparente: el contenido pasa por
 * debajo con el efecto de borde de Liquid Glass del sistema.
 */
export function largeTitleOptions(p: Palette): Options {
  return {
    headerLargeTitle: true,
    headerTransparent: Platform.OS === 'ios',
    headerShadowVisible: false,
    headerLargeTitleShadowVisible: false,
    headerStyle: { backgroundColor: Platform.OS === 'ios' ? 'transparent' : p.background },
    headerTitleStyle: { color: p.text },
    headerLargeTitleStyle: { color: p.text },
    headerTintColor: p.accent,
    contentStyle: { backgroundColor: p.background },
  };
}
