import { brand } from './brand';

export interface Palette {
  scheme: 'light' | 'dark';
  background: string;
  backgroundElevated: string;
  surface: string;
  surfaceBorder: string;
  text: string;
  textSecondary: string;
  textTertiary: string;
  separator: string;
  /** Relleno ámbar (botones, selección). Encima va `onPrimary`. */
  primary: string;
  onPrimary: string;
  /** Ámbar legible como texto/icono sobre el fondo de este modo. */
  accent: string;
  /** Amarillo pastel para superficies. Encima va `onPrimary`. */
  pastel: string;
  /** Tinte suave del ámbar para indicadores y fondos seleccionados. */
  primarySoft: string;
  danger: string;
  dangerDeep: string;
  warning: string;
  success: string;
  /** Fondo translúcido para simular el vidrio donde no hay Liquid Glass. */
  glassFallback: string;
  /** Degradado de fondo de las pantallas (se ve a través del vidrio). */
  ambient: [string, string, string];
}

const shared = {
  primary: brand.primary,
  onPrimary: brand.onPrimary,
  pastel: brand.pastel,
  danger: brand.danger,
  dangerDeep: brand.dangerDeep,
  warning: brand.warning,
  success: brand.success,
};

export const light: Palette = {
  ...shared,
  scheme: 'light',
  background: '#FFFBF2',
  backgroundElevated: '#FFFFFF',
  surface: '#FFFFFF',
  surfaceBorder: 'rgba(120,80,0,0.08)',
  text: '#2E2437',
  textSecondary: '#6A5F6E',
  textTertiary: '#A59BA3',
  separator: 'rgba(90,70,30,0.16)',
  accent: brand.accentLight,
  primarySoft: 'rgba(255,169,40,0.22)',
  glassFallback: 'rgba(255,255,255,0.78)',
  ambient: ['#FFEFC2', '#FFFBF2', '#FFF5D6'],
};

export const dark: Palette = {
  ...shared,
  scheme: 'dark',
  background: '#0D0B07',
  backgroundElevated: '#1A1610',
  surface: '#1E1A12',
  surfaceBorder: 'rgba(255,220,150,0.09)',
  text: '#FFF8E8',
  textSecondary: '#C2B59A',
  textTertiary: '#7D725C',
  separator: 'rgba(255,230,180,0.14)',
  accent: brand.accentDark,
  primarySoft: 'rgba(255,169,40,0.28)',
  glassFallback: 'rgba(38,32,20,0.72)',
  ambient: ['#2B2008', '#0D0B07', '#1E1705'],
};
