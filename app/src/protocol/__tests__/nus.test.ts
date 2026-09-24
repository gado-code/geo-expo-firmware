/// <reference types="node" />
import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
  LineBuffer,
  asciiToBase64,
  base64ToAscii,
  beaconCommand,
  parseDeviceLine,
} from '../nus.ts';

test('mensajes del contrato de hoy (§4.1)', () => {
  assert.deepEqual(parseDeviceLine('ALERT:1'), { kind: 'alert', level: 1, position: null });
  assert.deepEqual(parseDeviceLine('ALERT:2\n'), { kind: 'alert', level: 2, position: null });
  assert.deepEqual(parseDeviceLine('CANCEL'), { kind: 'cancel' });
});

test('mayúsculas/minúsculas y espacios no importan', () => {
  assert.deepEqual(parseDeviceLine('  alert:1\r'), { kind: 'alert', level: 1, position: null });
  assert.deepEqual(parseDeviceLine('cancel'), { kind: 'cancel' });
});

test('lo desconocido se ignora en silencio (§6)', () => {
  for (const l of ['', 'ALERT:3', 'ALERT', 'ALERT:12', 'BATT:80', 'hola', 'CANCELAR']) {
    assert.equal(parseDeviceLine(l), null, l);
  }
});

test('formato con GPS propuesto en §7, con longitud negativa (Colombia)', () => {
  assert.deepEqual(parseDeviceLine('ALERT:1;4.205760;-74.094648'), {
    kind: 'alert',
    level: 1,
    position: { lat: 4.20576, lon: -74.094648 },
  });
});

test('una posición rota no tumba la alerta: llega sin posición', () => {
  for (const l of ['ALERT:2;;', 'ALERT:2;abc;1', 'ALERT:2;4.2', 'ALERT:2;95;10', 'ALERT:2;1;200']) {
    assert.deepEqual(parseDeviceLine(l), { kind: 'alert', level: 2, position: null }, l);
  }
});

test('CANCEL con basura detrás sigue siendo CANCEL (corta por el primer ;)', () => {
  assert.deepEqual(parseDeviceLine('CANCEL;4.2;-74.1'), { kind: 'cancel' });
});

test('LineBuffer parte por \\n aunque el mensaje llegue troceado', () => {
  const b = new LineBuffer();
  assert.deepEqual(b.push('ALE'), []);
  assert.deepEqual(b.push('RT:1\nCAN'), ['ALERT:1']);
  assert.deepEqual(b.push('CEL\n'), ['CANCEL']);
  assert.deepEqual(b.push('ALERT:2\r\nALERT:1\n\n'), ['ALERT:2', 'ALERT:1']);
});

test('LineBuffer no crece sin fin si nunca llega \\n', () => {
  const b = new LineBuffer(16);
  b.push('x'.repeat(40));
  assert.deepEqual(b.push('ALERT:1\n'), ['ALERT:1']);
});

test('comandos de baliza (§4.2)', () => {
  assert.equal(beaconCommand(true), 'BEACON:ON');
  assert.equal(beaconCommand(false), 'BEACON:OFF');
});

test('base64 ida y vuelta, y coincide con Buffer', () => {
  for (const s of ['', 'A', 'AB', 'ABC', 'BEACON:ON', 'BEACON:OFF\n', 'ALERT:1;4.205760;-74.094648\n']) {
    const enc = asciiToBase64(s);
    assert.equal(enc, Buffer.from(s, 'latin1').toString('base64'), s);
    assert.equal(base64ToAscii(enc), s);
  }
});
