/**
 * BRANDING DE IVY.
 *
 * Todo el color de la app sale de aquí: ninguna pantalla lleva colores
 * escritos a mano. Los tres primeros son los del logo (assets/brand/logo.svg).
 */
export const brand = {
  name: 'Ivy',

  /** Ámbar de la hoja: relleno de botones principales, selección, acentos. */
  primary: '#FFA928',
  /** Tinta del contorno del logo. Va encima del ámbar: el blanco no se lee. */
  ink: '#2E2437',
  onPrimary: '#2E2437',
  /** Crema de las nervaduras. */
  cream: '#FFF6E6',
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
