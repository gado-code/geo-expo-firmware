import { GlassView } from 'expo-glass-effect';
import type { ReactNode } from 'react';
import { ActivityIndicator, Platform, Pressable, StyleSheet, View } from 'react-native';

import { hasLiquidGlass } from './GlassCard';
import { Text } from './Text';
import { radius, usePalette } from '@/theme';

type Kind = 'primary' | 'danger' | 'glass' | 'plain';

/**
 * Botón. En iOS 26 los principales son vidrio tintado e interactivo (el
 * brillo que sigue al dedo); en Android, botones Material con ripple.
 */
export function Button({
  title,
  onPress,
  kind = 'primary',
  icon,
  disabled,
  loading,
  large,
  onDark,
}: {
  title: string;
  onPress: () => void;
  kind?: Kind;
  icon?: ReactNode;
  disabled?: boolean;
  loading?: boolean;
  large?: boolean;
  /** Sobre fondos oscuros o de color (pantalla de alerta): texto blanco. */
  onDark?: boolean;
}) {
  const p = usePalette();
  const fill = kind === 'primary' ? p.primary : kind === 'danger' ? p.danger : undefined;
  const textTone =
    kind === 'primary' ? 'onPrimary' : kind === 'danger' || onDark ? 'inverse' : kind === 'plain' ? 'brand' : 'primary';
  const height = large ? 60 : 50;

  const content = (
    <View style={styles.row}>
      {loading ? <ActivityIndicator color={kind === 'primary' ? p.onPrimary : fill || onDark ? '#fff' : p.text} /> : icon}
      <Text variant="headline" tone={textTone} style={large && { fontSize: 19 }}>
        {title}
      </Text>
    </View>
  );

  if (hasLiquidGlass && kind !== 'plain') {
    return (
      <Pressable onPress={onPress} disabled={disabled || loading} style={{ opacity: disabled ? 0.45 : 1 }}>
        <GlassView
          isInteractive
          glassEffectStyle="regular"
          tintColor={fill}
          colorScheme={onDark ? 'dark' : 'auto'}
          style={[styles.base, { height, borderRadius: height / 2 }]}
        >
          {content}
        </GlassView>
      </Pressable>
    );
  }

  return (
    <Pressable
      onPress={onPress}
      disabled={disabled || loading}
      android_ripple={{ color: kind === 'primary' ? 'rgba(43,28,0,0.14)' : 'rgba(255,255,255,0.25)', borderless: false }}
      style={({ pressed }) => [
        styles.base,
        {
          height,
          borderRadius: Platform.OS === 'android' ? radius.md : height / 2,
          backgroundColor:
            fill ?? (kind === 'plain' ? 'transparent' : onDark ? 'rgba(255,255,255,0.18)' : p.glassFallback),
          borderColor: kind === 'glass' ? (onDark ? 'rgba(255,255,255,0.35)' : p.surfaceBorder) : 'transparent',
          opacity: disabled ? 0.45 : pressed && Platform.OS === 'ios' ? 0.75 : 1,
        },
      ]}
    >
      {content}
    </Pressable>
  );
}

const styles = StyleSheet.create({
  base: {
    overflow: 'hidden',
    justifyContent: 'center',
    alignItems: 'center',
    paddingHorizontal: 20,
    borderWidth: StyleSheet.hairlineWidth,
    borderCurve: 'continuous',
  },
  row: { flexDirection: 'row', alignItems: 'center', gap: 10 },
});
