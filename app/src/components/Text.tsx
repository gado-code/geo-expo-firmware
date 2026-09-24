import { Text as RNText, type TextProps } from 'react-native';

import { usePalette } from '@/theme';

type Variant = 'hero' | 'title' | 'headline' | 'body' | 'callout' | 'footnote' | 'caption';
type Tone = 'primary' | 'secondary' | 'tertiary' | 'brand' | 'danger' | 'success' | 'inverse';

const sizes: Record<Variant, { fontSize: number; lineHeight: number; fontWeight: '400' | '500' | '600' | '700' | '800' }> = {
  hero: { fontSize: 34, lineHeight: 40, fontWeight: '800' },
  title: { fontSize: 22, lineHeight: 28, fontWeight: '700' },
  headline: { fontSize: 17, lineHeight: 22, fontWeight: '600' },
  body: { fontSize: 17, lineHeight: 23, fontWeight: '400' },
  callout: { fontSize: 15, lineHeight: 20, fontWeight: '400' },
  footnote: { fontSize: 13, lineHeight: 18, fontWeight: '400' },
  caption: { fontSize: 12, lineHeight: 16, fontWeight: '500' },
};

export function Text({
  variant = 'body',
  tone = 'primary',
  style,
  ...rest
}: TextProps & { variant?: Variant; tone?: Tone }) {
  const p = usePalette();
  const color = {
    primary: p.text,
    secondary: p.textSecondary,
    tertiary: p.textTertiary,
    brand: p.primary,
    danger: p.danger,
    success: p.success,
    inverse: '#FFFFFF',
  }[tone];
  return <RNText {...rest} style={[sizes[variant], { color }, style]} />;
}
