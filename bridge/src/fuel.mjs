import { readFile } from 'node:fs/promises';

const SOURCE = 'European Commission Weekly Oil Bulletin';
const SOURCE_URL = 'https://energy.ec.europa.eu/data-and-analysis/weekly-oil-bulletin_en';
const datePattern = /^20\d{2}-(0[1-9]|1[0-2])-([0-2]\d|3[01])$/;

function validPrice(value, required) {
  if (value === null && !required) return true;
  return Number.isInteger(value) && value >= 500 && value <= 5000;
}

export function parseFuelSnapshot(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return null;
  const keys = Object.keys(value).sort();
  const expected = ['country', 'diesel_millieur_per_litre', 'lpg_millieur_per_litre', 'period_date',
    'schema', 'source', 'source_url', 'unleaded95_millieur_per_litre'].sort();
  if (keys.length !== expected.length || keys.some((key, index) => key !== expected[index])) return null;
  if (value.schema !== 1 || value.country !== 'GR' || !datePattern.test(value.period_date) ||
      value.source !== SOURCE || value.source_url !== SOURCE_URL ||
      !validPrice(value.unleaded95_millieur_per_litre, true) ||
      !validPrice(value.diesel_millieur_per_litre, true) ||
      !validPrice(value.lpg_millieur_per_litre, false)) return null;
  const date = new Date(`${value.period_date}T00:00:00Z`);
  if (Number.isNaN(date.valueOf()) || date.toISOString().slice(0, 10) !== value.period_date) return null;
  return { ...value };
}

export async function readFuelSnapshot(path) {
  try {
    const raw = await readFile(path, { encoding: 'utf8' });
    if (Buffer.byteLength(raw) > 8192) return null;
    return parseFuelSnapshot(JSON.parse(raw));
  } catch {
    return null;
  }
}

export const fuelSource = Object.freeze({ name: SOURCE, url: SOURCE_URL });
