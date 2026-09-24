import { useColorScheme } from 'react-native';

import { dark, light, type Palette } from './palette';

export { brand } from './brand';
export type { Palette } from './palette';

export function usePalette(): Palette {
  return useColorScheme() === 'dark' ? dark : light;
}

export const radius = { sm: 12, md: 18, lg: 26, xl: 34, pill: 999 } as const;
export const space = { xs: 4, sm: 8, md: 12, lg: 16, xl: 24, xxl: 32 } as const;
