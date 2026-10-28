import assert from 'node:assert/strict';
import { test } from 'node:test';
import { parseFuelSnapshot } from '../src/fuel.mjs';

const valid = {
  schema: 1,
  country: 'GR',
  period_date: '2026-10-19',
  source: 'European Commission Weekly Oil Bulletin',
  source_url: 'https://energy.ec.europa.eu/data-and-analysis/weekly-oil-bulletin_en',
  unleaded95_millieur_per_litre: 2191,
  diesel_millieur_per_litre: 2126,
  lpg_millieur_per_litre: null,
};

test('fuel cache accepts only the bounded official schema', () => {
  assert.deepEqual(parseFuelSnapshot(valid), valid);
  assert.equal(parseFuelSnapshot({ ...valid, country: 'XX' }), null);
  assert.equal(parseFuelSnapshot({ ...valid, unleaded95_millieur_per_litre: 99 }), null);
  assert.equal(parseFuelSnapshot({ ...valid, source_url: 'https://example.test/' }), null);
  assert.equal(parseFuelSnapshot({ ...valid, extra: true }), null);
});
