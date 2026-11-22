import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const html = fs.readFileSync(new URL('../web/index.html', import.meta.url), 'utf8');
const functions = ['itemText', 'openingRecord', 'chestHistory', 'exploredNamesAt', 'discText', 'discKnown', 'tombstoneHint', 'applyObjects'];
const source = functions.map(name => {
  const start = html.indexOf(`  function ${name}(`);
  const end = html.indexOf('\n  }', start);
  assert.ok(start >= 0 && end > start, `Production helper ${name} exists`);
  return html.slice(start, end + 4);
}).join('\n');

function context() {
  const state = {
    language: 'en', DISC: null, EXP: null, findsData: null,
    t: (text, args = {}) => text.replace('{count}', args.count),
    fmtDay: value => `day-${value}`, fmtAgo: value => `ago-${value}`, lootState: () => ({left: 5}),
  };
  state.discNames = () => [...new Set([...Object.keys(state.DISC?.players || {}), ...Object.keys(state.EXP?.players || {})])];
  state.exploredAt = (x, z) => state.exploredNamesAt(x, z).some(name => state.discNames().includes(name));
  vm.createContext(state);
  vm.runInContext(source, state);
  return state;
}

test('opening author remains visible while a chest is full or empty', () => {
  const c = context();
  const record = {by: 'Alice', at: 10};
  for (const empty of [false, true]) {
    const tip = c.chestHistory(record, {empty, at: 20});
    assert.ok(tip.includes(empty ? 'Looted' : 'Still full'));
    assert.ok(tip.includes('Opened by Alice, day-10'));
    assert.ok(tip.includes('Observed on day-20'));
  }
});

test('unknown actors are explicit and player names cannot inject tooltip HTML', () => {
  const c = context();
  assert.ok(c.chestHistory({by: '', at: 1}, null).includes('Unknown player'));
  const tip = c.chestHistory({by: '<img src=x> & Alice', at: 1}, null);
  assert.ok(tip.includes('&lt;img src=x&gt; &amp; Alice'));
  assert.ok(!tip.includes('<img'));
  assert.ok(!c.chestHistory(null, {empty: true, at: 1}).includes('Opened by'));
});

test('world-only chests can recover the matching saved opening', () => {
  const c = context();
  c.findsData = {chests: [{x: 101, z: 100, by: 'Storage owner', rl: false}, {x: 100, z: 100, by: 'Alice'}, {x: 110, z: 110, by: 'Bob'}]};
  assert.equal(c.openingRecord({x: 101, z: 100}).by, 'Alice');
  assert.equal(c.openingRecord({x: 300, z: 300}), null);
});

test('unkeyed items list per-player explored areas including offline history', () => {
  const c = context();
  c.EXP = {cells: 4, cellMetres: 20, players: {'Alice & Bob': new Uint8Array([0b00100000, 0]), Offline: new Uint8Array([0b00100000, 0])}};
  const tip = c.discText([0, 25, 0, 25]);
  assert.ok(tip.includes('Area explored by Alice &amp; Bob, Offline'));
  assert.ok(!tip.includes('Opened by'));
  assert.equal(c.discText([0, -1, 0, 25]), '');
});

test('unsupported discovery layouts fall back to explored areas without inventing knowledge', () => {
  const c = context();
  const point = Object.assign([0, 25, 0, 25], {k: 42});
  c.DISC = {live: false, players: {Alice: {keys: new Set()}}};
  c.EXP = {cells: 4, cellMetres: 20, players: {Alice: new Uint8Array([0b00100000, 0])}};
  assert.ok(c.discText(point).includes('Area explored by Alice'));
  assert.equal(c.discKnown(point), true);
  c.EXP = null;
  assert.equal(c.discText(point), '');
  assert.equal(c.discKnown(point), null);
});

test('confirmed discovery keys keep their original attribution and filtering', () => {
  const c = context();
  const point = Object.assign([0, 25, 0, 25], {k: 42});
  c.DISC = {live: true, players: {Alice: {keys: new Set([42])}, Bob: {keys: new Set()}}};
  assert.equal(c.discText(point), ' · Discovered by Alice');
  assert.equal(c.discKnown(point), true);
  c.DISC.live = false;
  assert.equal(c.discKnown(point), true); // Existing persisted knowledge remains valid.
  c.DISC.live = true;
  point.k = 99;
  assert.equal(c.discKnown(point), false);
  assert.equal(c.discText(point), ' · Not discovered yet');
});

test('tombstones exclude confirmed collections but retain unobserved last-known markers', () => {
  const c = context();
  const data = {categories: [], pois: []};
  c.applyObjects(data, {chests: [], switches: [], tombstones: [
    {key: 'a', x: 100, y: 40, z: 200, loaded: true, collected: false},
    {key: 'b', x: 101, y: 40, z: 200, loaded: false, collected: false},
    {key: 'c', x: 102, y: 40, z: 200, loaded: false, collected: true},
  ]});
  assert.equal(data.pois.length, 2);
  assert.equal(data.categories[0].id, 'tombstone');
  assert.equal(data.pois[1][5].tombstone.loaded, false);
  assert.equal(data.pois[0].isFind, true);
});

test('tombstone hints distinguish direct ownership, nearby deaths and unknown status', () => {
  const c = context();
  assert.ok(c.tombstoneHint({owner: 'Alice', ownerSource: 'entity', loaded: true, seen: 10}).includes('Owner: Alice'));
  const hint = c.tombstoneHint({owner: 'Alice <x>', ownerSource: 'nearby-death', loaded: false, seen: 10});
  assert.ok(hint.includes('Possible owner: Alice &lt;x&gt; (nearby death)'));
  assert.ok(hint.includes('Last known position; not currently observed'));
  assert.ok(c.tombstoneHint({owner: '', loaded: true, seen: 10}).includes('Owner unknown'));
});
