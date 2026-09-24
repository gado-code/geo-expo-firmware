/**
 * Máquina de estados de una alerta, del lado de la app (§5 de
 * INTEGRACION-APP.md). Pura: recibe eventos con su marca de tiempo y devuelve
 * el estado nuevo, así se prueba sin temporizadores reales.
 *
 *   idle ──ALERT:x──▶ countdown ──CANCEL──▶ cancelled
 *                        │
 *                        ├──(plazo cumplido)──▶ confirmed
 *                        └──(usuario: «avisar ya»)──▶ confirmed
 *
 * El firmware no manda nada al expirar su ventana: el silencio ES la
 * confirmación. Por eso la app confirma por su cuenta al vencer el plazo.
 */
import { DEVICE_CANCEL_WINDOW_MS, type AlertLevel, type Position } from './nus.ts';

export type AlertSource = 'device' | 'demo';

export interface ActiveAlert {
  id: string;
  level: AlertLevel;
  source: AlertSource;
  startedAt: number;
  deadline: number;
  devicePosition: Position | null;
}

export type AlertState =
  | { phase: 'idle' }
  | { phase: 'countdown'; alert: ActiveAlert }
  | { phase: 'confirmed'; alert: ActiveAlert; confirmedAt: number; early: boolean }
  | { phase: 'cancelled'; alert: ActiveAlert; cancelledAt: number; by: 'device' | 'app' };

export type AlertEvent =
  | { type: 'ALERT'; level: AlertLevel; position: Position | null; source: AlertSource; at: number; id: string }
  | { type: 'DEVICE_CANCEL'; at: number }
  | { type: 'APP_CANCEL'; at: number }
  | { type: 'CONFIRM_NOW'; at: number }
  | { type: 'TICK'; at: number }
  | { type: 'DISMISS' };

export const initialAlertState: AlertState = { phase: 'idle' };

/** Nunca por debajo de la ventana del firmware. */
export function effectiveCountdownMs(configuredSeconds: number): number {
  const ms = Math.round(configuredSeconds * 1000);
  return Number.isFinite(ms) ? Math.max(DEVICE_CANCEL_WINDOW_MS, ms) : DEVICE_CANCEL_WINDOW_MS;
}

export function reduceAlert(state: AlertState, event: AlertEvent, countdownMs: number): AlertState {
  switch (event.type) {
    case 'ALERT': {
      // Una alerta confirmada no se pisa: ya estamos avisando a los contactos.
      if (state.phase === 'confirmed') return state;
      // ALERT:2 llega 3 s después de empezar a mantener: si ya había cuenta
      // atrás, se escala el nivel sin reiniciar el plazo hacia atrás.
      if (state.phase === 'countdown') {
        const level = Math.max(state.alert.level, event.level) as AlertLevel;
        return {
          phase: 'countdown',
          alert: {
            ...state.alert,
            level,
            devicePosition: event.position ?? state.alert.devicePosition,
            deadline: Math.max(state.alert.deadline, event.at + countdownMs),
          },
        };
      }
      return {
        phase: 'countdown',
        alert: {
          id: event.id,
          level: event.level,
          source: event.source,
          startedAt: event.at,
          deadline: event.at + countdownMs,
          devicePosition: event.position,
        },
      };
    }
    case 'DEVICE_CANCEL':
      // CANCEL anula la última alerta (§5.2). Si ya se confirmó, llega tarde:
      // se ignora, porque los avisos ya pueden haber salido.
      if (state.phase !== 'countdown') return state;
      return { phase: 'cancelled', alert: state.alert, cancelledAt: event.at, by: 'device' };
    case 'APP_CANCEL':
      if (state.phase !== 'countdown') return state;
      return { phase: 'cancelled', alert: state.alert, cancelledAt: event.at, by: 'app' };
    case 'CONFIRM_NOW':
      if (state.phase !== 'countdown') return state;
      return { phase: 'confirmed', alert: state.alert, confirmedAt: event.at, early: true };
    case 'TICK':
      if (state.phase !== 'countdown' || event.at < state.alert.deadline) return state;
      return { phase: 'confirmed', alert: state.alert, confirmedAt: event.at, early: false };
    case 'DISMISS':
      return state.phase === 'countdown' ? state : initialAlertState;
  }
}

export function remainingMs(state: AlertState, now: number): number {
  return state.phase === 'countdown' ? Math.max(0, state.alert.deadline - now) : 0;
}
