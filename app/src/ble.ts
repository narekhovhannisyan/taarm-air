import { Buffer } from 'buffer';

export const DEVICE_NAME = 'taarm-thermo';

export const UUIDS = {
  service: 'a1b30001-c3d4-4e5f-8a9b-0c1d2e3f4a5b',
  temperature: 'a1b30002-c3d4-4e5f-8a9b-0c1d2e3f4a5b',
  setpoint: 'a1b30003-c3d4-4e5f-8a9b-0c1d2e3f4a5b',
  mode: 'a1b30004-c3d4-4e5f-8a9b-0c1d2e3f4a5b',
  fanDuty: 'a1b30005-c3d4-4e5f-8a9b-0c1d2e3f4a5b',
  rpm: 'a1b30006-c3d4-4e5f-8a9b-0c1d2e3f4a5b',
} as const;

export function decodeFloatLE(base64: string | null | undefined): number | null {
  if (!base64) return null;
  const buf = Buffer.from(base64, 'base64');
  if (buf.length < 4) return null;
  return buf.readFloatLE(0);
}

export function encodeFloatLE(value: number): string {
  const buf = Buffer.alloc(4);
  buf.writeFloatLE(value, 0);
  return buf.toString('base64');
}

export function decodeUint8(base64: string | null | undefined): number | null {
  if (!base64) return null;
  const buf = Buffer.from(base64, 'base64');
  if (buf.length < 1) return null;
  return buf.readUInt8(0);
}

export function encodeUint8(value: number): string {
  const buf = Buffer.alloc(1);
  buf.writeUInt8(value & 0xff, 0);
  return buf.toString('base64');
}

export function decodeUint16LE(base64: string | null | undefined): number | null {
  if (!base64) return null;
  const buf = Buffer.from(base64, 'base64');
  if (buf.length < 2) return null;
  return buf.readUInt16LE(0);
}
