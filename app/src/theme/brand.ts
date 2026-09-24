/**
 * BRANDING PROVISIONAL.
 *
 * Todo el color de la app sale de aquí. Cuando llegue el branding definitivo
 * basta con cambiar estos valores (y el logo en src/components/BrandMark.tsx y
 * assets/): ninguna pantalla lleva colores escritos a mano.
 */
export const brand = {
  name: 'GEO-EXPO',
  product: 'Alert',

  /** Color de marca: acentos, botones principales, pestaña activa. */
  primary: '#2E6BFF',
  /** Segundo color de marca, para degradados y detalles. */
  secondary: '#00C2A8',

  /** Rojo de alerta. No lo sustituyas por el color de marca: tiene que asustar. */
  danger: '#FF3B30',
  dangerDeep: '#B3001B',
  warning: '#FF9F0A',
  success: '#30D158',
} as const;
