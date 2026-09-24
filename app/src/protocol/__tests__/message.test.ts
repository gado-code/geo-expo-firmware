/// <reference types="node" />
import assert from 'node:assert/strict';
import { test } from 'node:test';

import { buildAlertMessage, mapsLink } from '../../services/message.ts';

test('enlace de mapas con 6 decimales y signo', () => {
  assert.equal(mapsLink({ lat: 4.20576, lon: -74.094648 }), 'https://maps.google.com/?q=4.205760,-74.094648');
});

test('mensaje con posición y nivel 2', () => {
  const msg = buildAlertMessage({
    base: ' Necesito ayuda. ',
    level: 2,
    position: { lat: 4.2, lon: -74.1 },
    at: new Date(2026, 8, 24, 7, 5),
  });
  assert.equal(
    msg,
    'Necesito ayuda.\nEs una alerta prolongada: puede ser grave.\nHora: 07:05\nUbicación: https://maps.google.com/?q=4.200000,-74.100000',
  );
});

test('mensaje sin posición', () => {
  const msg = buildAlertMessage({ base: 'Ayuda', level: 1, position: null, at: new Date(2026, 0, 1, 23, 59) });
  assert.equal(msg, 'Ayuda\nHora: 23:59\nUbicación: no disponible');
});
