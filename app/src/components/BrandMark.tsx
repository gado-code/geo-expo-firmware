import { LinearGradient } from 'expo-linear-gradient';
import { StyleSheet, View } from 'react-native';

import { brand } from '@/theme';

/**
 * LOGO PROVISIONAL: un llavero (anilla + mando con su botón).
 * Cuando llegue el logo definitivo, sustituye este componente por un
 * <Image source={require('@/../assets/brand/logo.png')} /> del mismo tamaño;
 * se usa en la barra inferior (iOS), en Inicio, en la vinculación y en la alerta.
 */
export function BrandMark({ size = 36, pulse = false }: { size?: number; pulse?: boolean }) {
  const stroke = Math.max(1.5, size * 0.07);
  const ring = size * 0.3;
  const fobW = size * 0.38;
  const fobH = size * 0.5;
  const button = fobW * 0.52;
  return (
    <LinearGradient
      colors={[brand.primary, brand.secondary]}
      start={{ x: 0, y: 0 }}
      end={{ x: 1, y: 1 }}
      style={[styles.tile, { width: size, height: size, borderRadius: size * 0.3 }]}
    >
      {/* anilla */}
      <View
        style={{
          position: 'absolute',
          top: size * 0.14,
          left: size * 0.16,
          width: ring,
          height: ring,
          borderRadius: ring / 2,
          borderWidth: stroke,
          borderColor: 'rgba(255,255,255,0.9)',
        }}
      />
      {/* mando */}
      <View
        style={{
          position: 'absolute',
          top: size * 0.32,
          left: size * 0.36,
          width: fobW,
          height: fobH,
          borderRadius: fobW * 0.42,
          backgroundColor: '#FFFFFF',
          alignItems: 'center',
          justifyContent: 'center',
          transform: [{ rotate: '-18deg' }],
        }}
      >
        <View
          style={{
            width: button,
            height: button,
            borderRadius: button / 2,
            backgroundColor: pulse ? brand.danger : brand.primary,
          }}
        />
      </View>
    </LinearGradient>
  );
}

const styles = StyleSheet.create({
  tile: { alignItems: 'center', justifyContent: 'center', borderCurve: 'continuous', overflow: 'hidden' },
});
