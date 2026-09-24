/// <reference types="node" />
import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
  type AlertState,
  effectiveCountdownMs,
  initialAlertState,
  reduceAlert,
  remainingMs,
} from '../alertMachine.ts';

const MS = 10_000;
const alert = (at: number, level: 1 | 2 = 1) =>
  ({ type: 'ALERT', level, position: null, source: 'device', at, id: `a${at}` }) as const;

function run(events: Parameters<typeof reduceAlert>[1][], ms = MS): AlertState {
  return events.reduce((s, e) => reduceAlert(s, e, ms), initialAlertState);
}

test('ALERT abre la cuenta atrás; sin CANCEL, se confirma al vencer', () => {
  let s = run([alert(1000)]);
  assert.equal(s.phase, 'countdown');
  assert.equal(remainingMs(s, 4000), 7000);
  s = reduceAlert(s, { type: 'TICK', at: 10_999 }, MS);
  assert.equal(s.phase, 'countdown');
  s = reduceAlert(s, { type: 'TICK', at: 11_000 }, MS);
  assert.equal(s.phase, 'confirmed');
  assert.equal(s.phase === 'confirmed' && s.early, false);
});

test('CANCEL del llavero dentro de la ventana anula la alerta', () => {
  const s = run([alert(0), { type: 'DEVICE_CANCEL', at: 4000 }]);
  assert.equal(s.phase, 'cancelled');
  assert.equal(s.phase === 'cancelled' && s.by, 'device');
});

test('un CANCEL tardío no deshace una alerta ya confirmada', () => {
  const s = run([alert(0), { type: 'TICK', at: 10_000 }, { type: 'DEVICE_CANCEL', at: 10_200 }]);
  assert.equal(s.phase, 'confirmed');
});

test('CANCEL sin alerta previa no hace nada', () => {
  assert.equal(run([{ type: 'DEVICE_CANCEL', at: 1 }]).phase, 'idle');
});

test('«Avisar ya» confirma antes de tiempo', () => {
  const s = run([alert(0, 2), { type: 'CONFIRM_NOW', at: 2000 }]);
  assert.equal(s.phase, 'confirmed');
  assert.equal(s.phase === 'confirmed' && s.early, true);
});

test('tras cancelar, una alerta nueva empieza de cero', () => {
  const s = run([alert(0), { type: 'DEVICE_CANCEL', at: 500 }, alert(5000, 2)]);
  assert.equal(s.phase, 'countdown');
  assert.equal(s.phase === 'countdown' && s.alert.level, 2);
  assert.equal(s.phase === 'countdown' && s.alert.deadline, 15_000);
});

test('una segunda ALERT durante la cuenta atrás escala el nivel y no acorta el plazo', () => {
  const s = run([alert(0, 1), alert(3000, 2)]);
  assert.equal(s.phase === 'countdown' && s.alert.level, 2);
  assert.equal(s.phase === 'countdown' && s.alert.deadline, 13_000);
  assert.equal(s.phase === 'countdown' && s.alert.id, 'a0');
});

test('DISMISS no puede cerrar una cuenta atrás en curso', () => {
  assert.equal(run([alert(0), { type: 'DISMISS' }]).phase, 'countdown');
  assert.equal(run([alert(0), { type: 'APP_CANCEL', at: 1 }, { type: 'DISMISS' }]).phase, 'idle');
});

test('la cuenta atrás de la app nunca baja de los 10 s del firmware (§5.4)', () => {
  assert.equal(effectiveCountdownMs(3), 10_000);
  assert.equal(effectiveCountdownMs(10), 10_000);
  assert.equal(effectiveCountdownMs(20), 20_000);
  assert.equal(effectiveCountdownMs(Number.NaN), 10_000);
});
