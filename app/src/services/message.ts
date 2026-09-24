/** Texto del aviso a los contactos. Puro, para poder probarlo. */
import type { AlertLevel, Position } from '../protocol/nus.ts';

export function mapsLink(p: Position): string {
  return `https://maps.google.com/?q=${p.lat.toFixed(6)},${p.lon.toFixed(6)}`;
}

export function buildAlertMessage(opts: {
  base: string;
  level: AlertLevel;
  position: Position | null;
  at: Date;
}): string {
  const lines = [opts.base.trim()];
  if (opts.level === 2) lines.push('Es una alerta prolongada: puede ser grave.');
  const hh = String(opts.at.getHours()).padStart(2, '0');
  const mm = String(opts.at.getMinutes()).padStart(2, '0');
  lines.push(`Hora: ${hh}:${mm}`);
  lines.push(opts.position ? `Ubicación: ${mapsLink(opts.position)}` : 'Ubicación: no disponible');
  return lines.join('\n');
}
