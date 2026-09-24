import Svg, { Path } from 'react-native-svg';

import { brand } from '@/theme';

/**
 * Logo de Ivy: la hoja. Mismos trazos que assets/brand/logo.svg (el original),
 * recortados a la hoja para que llene el cuadro sin margen.
 *
 * `pulse` la tiñe de rojo mientras hay una alerta en curso (barra inferior).
 */
export function BrandMark({ size = 36, pulse = false }: { size?: number; pulse?: boolean }) {
  return (
    <Svg width={size} height={size} viewBox="-4 5 208 208" accessibilityLabel={brand.name}>
      <Path d="M100 120 L100 206" stroke={brand.ink} strokeWidth={8.5} strokeLinecap="round" fill="none" />
      <Path
        d="M100 120 C70 118 30 128 14 104 C34 96 46 76 40 50 C62 58 80 50 100 12 C120 50 138 58 160 50 C154 76 166 96 186 104 C170 128 130 118 100 120 Z"
        fill={pulse ? brand.danger : brand.primary}
        stroke={brand.ink}
        strokeWidth={7}
        strokeLinejoin="round"
      />
      <Path
        d="M100 114 L100 44 M100 97 L64 80 M100 97 L136 80"
        stroke={brand.cream}
        strokeWidth={7}
        strokeLinecap="round"
        fill="none"
      />
    </Svg>
  );
}
