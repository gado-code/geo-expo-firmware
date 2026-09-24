import type { ReactNode } from 'react';
import { Platform, Pressable, StyleSheet, View } from 'react-native';

import { Icon } from './Icon';
import { Text } from './Text';
import { usePalette } from '@/theme';

export function ListRow({
  title,
  subtitle,
  leading,
  trailing,
  onPress,
  chevron,
  destructive,
}: {
  title: string;
  subtitle?: string;
  leading?: ReactNode;
  trailing?: ReactNode;
  onPress?: () => void;
  chevron?: boolean;
  destructive?: boolean;
}) {
  const p = usePalette();
  return (
    <Pressable
      onPress={onPress}
      disabled={!onPress}
      android_ripple={{ color: p.separator }}
      style={({ pressed }) => [styles.row, pressed && Platform.OS === 'ios' && { opacity: 0.6 }]}
    >
      {leading}
      <View style={styles.texts}>
        <Text variant="body" tone={destructive ? 'danger' : 'primary'} numberOfLines={1}>
          {title}
        </Text>
        {subtitle ? (
          <Text variant="footnote" tone="secondary" numberOfLines={2}>
            {subtitle}
          </Text>
        ) : null}
      </View>
      {trailing}
      {chevron ? <Icon ios="chevron.right" android="chevron_right" size={14} color={p.textTertiary} weight="semibold" /> : null}
    </Pressable>
  );
}

export function RowSeparator() {
  const p = usePalette();
  return <View style={{ height: StyleSheet.hairlineWidth, backgroundColor: p.separator, marginLeft: 56 }} />;
}

export function IconBadge({ children, color }: { children: ReactNode; color: string }) {
  return <View style={[styles.badge, { backgroundColor: color }]}>{children}</View>;
}

const styles = StyleSheet.create({
  row: { flexDirection: 'row', alignItems: 'center', gap: 14, paddingVertical: 12, minHeight: 52 },
  texts: { flex: 1, gap: 2 },
  badge: {
    width: 32,
    height: 32,
    borderRadius: 9,
    borderCurve: 'continuous',
    alignItems: 'center',
    justifyContent: 'center',
  },
});
