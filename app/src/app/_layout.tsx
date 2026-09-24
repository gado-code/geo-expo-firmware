import { Stack } from 'expo-router';
import { StatusBar } from 'expo-status-bar';
import { GestureHandlerRootView } from 'react-native-gesture-handler';

import { KeychainProvider } from '@/state/KeychainProvider';
import { usePalette } from '@/theme';

export default function RootLayout() {
  const p = usePalette();
  return (
    <GestureHandlerRootView style={{ flex: 1, backgroundColor: p.background }}>
      <KeychainProvider>
        <StatusBar style="auto" />
        <Stack screenOptions={{ headerShown: false, contentStyle: { backgroundColor: p.background } }}>
          <Stack.Screen name="(tabs)" />
          <Stack.Screen
            name="alerta"
            options={{ presentation: 'fullScreenModal', gestureEnabled: false, animation: 'fade' }}
          />
          <Stack.Screen
            name="vincular"
            options={{
              presentation: 'formSheet',
              sheetAllowedDetents: [0.65, 1],
              sheetGrabberVisible: true,
              sheetCornerRadius: 32,
              contentStyle: { backgroundColor: 'transparent' },
            }}
          />
          <Stack.Screen
            name="contacto"
            options={{
              presentation: 'formSheet',
              sheetAllowedDetents: [0.6, 1],
              sheetGrabberVisible: true,
              sheetCornerRadius: 32,
              contentStyle: { backgroundColor: 'transparent' },
            }}
          />
        </Stack>
      </KeychainProvider>
    </GestureHandlerRootView>
  );
}
