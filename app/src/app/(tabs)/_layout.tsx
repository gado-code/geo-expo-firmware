import { NativeTabs } from 'expo-router/unstable-native-tabs';

import { KeychainAccessory } from '@/components/KeychainAccessory';
import { useKeychain } from '@/state/KeychainProvider';
import { usePalette } from '@/theme';

/**
 * Barra de pestañas NATIVA: en iOS 26 es la barra de Liquid Glass del sistema
 * (flotante, se encoge al hacer scroll) con el accesorio del logo encima; en
 * Android es la barra de navegación de Material 3.
 */
export default function TabsLayout() {
  const p = usePalette();
  const { contacts } = useKeychain();

  return (
    <NativeTabs
      tintColor={p.accent}
      minimizeBehavior="onScrollDown"
      // Android (Material 3)
      backgroundColor={p.scheme === 'dark' ? p.backgroundElevated : undefined}
      indicatorColor={p.primarySoft}
      labelVisibilityMode="labeled"
    >
      {/* iOS 26+: pastilla de vidrio sobre la barra con el logo y el estado del llavero. */}
      <NativeTabs.BottomAccessory>
        <KeychainAccessory />
      </NativeTabs.BottomAccessory>

      <NativeTabs.Trigger name="(inicio)">
        <NativeTabs.Trigger.Label>Llavero</NativeTabs.Trigger.Label>
        <NativeTabs.Trigger.Icon
          sf={{ default: 'sensor.tag.radiowaves.forward', selected: 'sensor.tag.radiowaves.forward.fill' }}
          md="sensors"
        />
      </NativeTabs.Trigger>

      <NativeTabs.Trigger name="alertas">
        <NativeTabs.Trigger.Label>Alertas</NativeTabs.Trigger.Label>
        <NativeTabs.Trigger.Icon sf={{ default: 'clock', selected: 'clock.fill' }} md="history" />
      </NativeTabs.Trigger>

      <NativeTabs.Trigger name="contactos">
        <NativeTabs.Trigger.Label>Contactos</NativeTabs.Trigger.Label>
        <NativeTabs.Trigger.Icon sf={{ default: 'person.2', selected: 'person.2.fill' }} md="group" />
        <NativeTabs.Trigger.Badge hidden={contacts.length > 0}>!</NativeTabs.Trigger.Badge>
      </NativeTabs.Trigger>

      <NativeTabs.Trigger name="ajustes">
        <NativeTabs.Trigger.Label>Ajustes</NativeTabs.Trigger.Label>
        <NativeTabs.Trigger.Icon sf={{ default: 'gearshape', selected: 'gearshape.fill' }} md="settings" />
      </NativeTabs.Trigger>
    </NativeTabs>
  );
}
