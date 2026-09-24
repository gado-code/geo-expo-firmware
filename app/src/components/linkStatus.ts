import type { LinkSnapshot } from '@/ble/KeychainLink';
import type { Palette } from '@/theme';

export interface StatusView {
  label: string;
  detail: string;
  color: string;
  live: boolean;
}

/** Texto y color del estado de conexión, igual en Inicio y en la barra inferior. */
export function describeLink(s: LinkSnapshot, p: Palette): StatusView {
  switch (s.status) {
    case 'connected':
      return { label: 'Conectado', detail: 'Protegido: tu llavero está listo', color: p.success, live: true };
    case 'connecting':
      return { label: 'Conectando…', detail: s.deviceName ?? 'Llavero', color: p.warning, live: false };
    case 'searching':
      return {
        label: 'Buscando…',
        detail: s.attempt > 2 ? 'Acerca el llavero al teléfono' : 'Reconectando con tu llavero',
        color: p.warning,
        live: false,
      };
    case 'unpaired':
      return { label: 'Sin vincular', detail: 'Vincula tu llavero para empezar', color: p.textTertiary, live: false };
    case 'poweredOff':
      return { label: 'Bluetooth apagado', detail: 'Enciéndelo para recibir alertas', color: p.danger, live: false };
    case 'unauthorized':
      return { label: 'Sin permiso', detail: 'Permite Bluetooth en Ajustes del sistema', color: p.danger, live: false };
    case 'unavailable':
      return {
        label: 'Bluetooth no disponible',
        detail: 'Necesitas la build de desarrollo (no Expo Go). Puedes probar en modo demo.',
        color: p.textTertiary,
        live: false,
      };
  }
}
