import { GlassView, isLiquidGlassAvailable } from 'expo-glass-effect';
import type { ReactNode } from 'react';
import { Platform, StyleSheet, View, type ColorValue, type StyleProp, type ViewStyle } from 'react-native';

import { radius, usePalette } from '@/theme';

const liquidGlass = Platform.OS === 'ios' && isLiquidGlassAvailable();

/**
 * Tarjeta de vidrio. En iOS 26+ es Liquid Glass de verdad (UIGlassEffect);
 * en iOS anteriores y Android, una superficie translúcida con el mismo radio y
 * relleno, para que la maqueta sea idéntica en todas partes.
 */
export function GlassCard({
  children,
  style,
  tint,
  interactive,
  padded = true,
}: {
  children: ReactNode;
  style?: StyleProp<ViewStyle>;
  tint?: ColorValue;
  interactive?: boolean;
  padded?: boolean;
}) {
  const p = usePalette();
  const base = [styles.card, padded && styles.padded, style];

  if (liquidGlass) {
    return (
      <GlassView glassEffectStyle="regular" tintColor={tint} isInteractive={interactive} style={base}>
        {children}
      </GlassView>
    );
  }
  return (
    <View
      style={[
        base,
        {
          backgroundColor: tint ?? p.glassFallback,
          borderColor: p.surfaceBorder,
        },
        Platform.OS === 'android' ? styles.androidElevation : styles.softShadow,
      ]}
    >
      {children}
    </View>
  );
}

export const hasLiquidGlass = liquidGlass;

const styles = StyleSheet.create({
  card: {
    borderRadius: radius.lg,
    borderCurve: 'continuous',
    overflow: 'hidden',
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: 'transparent',
  },
  padded: { padding: 18 },
  androidElevation: { elevation: 1 },
  softShadow: {
    shadowColor: '#000',
    shadowOpacity: 0.06,
    shadowRadius: 16,
    shadowOffset: { width: 0, height: 6 },
  },
});
