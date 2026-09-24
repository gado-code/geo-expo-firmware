import { Stack } from 'expo-router';

import { largeTitleOptions } from '@/navigation/stack';
import { usePalette } from '@/theme';

export default function Layout() {
  const p = usePalette();
  return (
    <Stack screenOptions={largeTitleOptions(p)}>
      <Stack.Screen name="index" options={{ title: 'Mi llavero' }} />
    </Stack>
  );
}
