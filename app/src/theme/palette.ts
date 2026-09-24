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
  primary: string;
  primaryText: string;
  secondary: string;
  danger: string;
  dangerDeep: string;
  warning: string;
  success: string;
  /** Fondo translúcido para simular el vidrio donde no hay Liquid Glass. */
  glassFallback: string;
  /** Degradado de fondo de las pantallas (se ve a través del vidrio). */
  ambient: [string, string, string];
}

export const light: Palette = {
  scheme: 'light',
  background: '#F2F3F7',
  backgroundElevated: '#FFFFFF',
  surface: '#FFFFFF',
  surfaceBorder: 'rgba(0,0,0,0.06)',
  text: '#0B0D12',
  textSecondary: '#5B6170',
  textTertiary: '#9AA0AD',
  separator: 'rgba(60,60,67,0.18)',
  primary: brand.primary,
  primaryText: '#FFFFFF',
  secondary: brand.secondary,
  danger: brand.danger,
  dangerDeep: brand.dangerDeep,
  warning: brand.warning,
  success: brand.success,
  glassFallback: 'rgba(255,255,255,0.78)',
  ambient: ['#E9EFFF', '#F2F3F7', '#E6FBF7'],
};

export const dark: Palette = {
  scheme: 'dark',
  background: '#07080C',
  backgroundElevated: '#12141B',
  surface: '#15171F',
  surfaceBorder: 'rgba(255,255,255,0.08)',
  text: '#F5F6FA',
  textSecondary: '#A4AAB8',
  textTertiary: '#6B7080',
  separator: 'rgba(84,84,88,0.5)',
  primary: brand.primary,
  primaryText: '#FFFFFF',
  secondary: brand.secondary,
  danger: brand.danger,
  dangerDeep: brand.dangerDeep,
  warning: brand.warning,
  success: brand.success,
  glassFallback: 'rgba(28,30,38,0.72)',
  ambient: ['#0B1330', '#07080C', '#04201D'],
};
