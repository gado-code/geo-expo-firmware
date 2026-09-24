import { SymbolView, type SymbolViewProps } from 'expo-symbols';
import type { ColorValue } from 'react-native';

type Name = Extract<SymbolViewProps['name'], { ios?: unknown }>;

/**
 * Un icono, dos catálogos: SF Symbols en iPhone y Material Symbols en Android.
 */
export function Icon({
  ios,
  android,
  size = 22,
  color,
  weight,
}: {
  ios: NonNullable<Name['ios']>;
  android: NonNullable<Name['android']>;
  size?: number;
  color: ColorValue;
  weight?: 'regular' | 'medium' | 'semibold' | 'bold';
}) {
  return (
    <SymbolView
      name={{ ios, android, web: android }}
      size={size}
      tintColor={color}
      weight={weight}
      resizeMode="scaleAspectFit"
    />
  );
}
