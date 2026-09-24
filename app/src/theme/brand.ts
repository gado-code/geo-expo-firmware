/**
 * BRANDING DE IVY.
 *
 * Todo el color de la app sale de aquí: ninguna pantalla lleva colores
 * escritos a mano. El logo provisional está en src/components/BrandMark.tsx.
 */
export const brand = {
  name: 'Ivy',

  /** Ámbar: relleno de botones principales, selección, acentos. */
  primary: '#F5A30B',
  /** Tinta sobre el ámbar. El blanco sobre ámbar no se lee bien. */
  onPrimary: '#2B1C00',
  /** Ámbar oscuro para texto e iconos sobre fondos claros. */
  accentLight: '#A86200',
  /** Ámbar claro para texto e iconos sobre fondos oscuros. */
  accentDark: '#FFC247',
  /** Amarillo pastel: superficies suaves, degradados, detalles. */
  pastel: '#FFE9A3',

  /** Rojo de alerta. No lo sustituyas por el color de marca: tiene que asustar. */
  danger: '#FF3B30',
  dangerDeep: '#B3001B',
  /** Naranja (distinto del ámbar) para «buscando» y la baliza sonando. */
  warning: '#FF7A1A',
  success: '#30D158',
} as const;
