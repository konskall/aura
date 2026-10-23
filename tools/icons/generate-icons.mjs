import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

const root = process.cwd();
const require = createRequire(path.join(root, '.tools', 'icon-build', 'package.json'));
const { Resvg } = require('@resvg/resvg-js');
const { PNG } = require('pngjs');

const sourceDir = path.join(root, '.tools', 'icon-build', 'node_modules', 'lucide-static', 'icons');
const outputDir = path.join(root, 'shared', 'assets', 'icons');
// v12: 64 px sources stay crisp from 24 px status glyphs up to 64 px hero icons.
const size = 64;
fs.mkdirSync(outputDir, { recursive: true });

const roleMap = {
  Home: 'home', Back: 'arrow-left', Forward: 'arrow-right', Menu: 'menu', Close: 'x',
  Apps: 'layout-grid', Chevron: 'chevron-right', Refresh: 'rotate-cw', More: 'ellipsis',
  Microphone: 'mic', MicMuted: 'mic-off', Chat: 'message-circle', Search: 'search', Send: 'send',
  Cancel: 'circle-x', Thinking: 'brain', Copy: 'copy', Confirmation: 'circle-check', Voice: 'audio-lines',
  Play: 'play', Pause: 'pause', Stop: 'square', Speaker: 'volume-2', VolumeDown: 'volume-1',
  VolumeUp: 'volume-2', Muted: 'volume-x', TestSound: 'audio-waveform', Record: 'circle',
  Message: 'message-square', Inbox: 'inbox', Unread: 'mail', Notification: 'bell', Compose: 'square-pen',
  Reply: 'reply', Room: 'users', Lock: 'lock', Encrypted: 'shield-check', Connection: 'link',
  Wifi: 'wifi', WifiOff: 'wifi-off', Bluetooth: 'bluetooth', Battery: 'battery-medium',
  Charging: 'battery-charging', Display: 'monitor', Brightness: 'sun', Settings: 'settings',
  Storage: 'hard-drive', Cpu: 'cpu', Memory: 'memory-stick', Weather: 'cloud-sun', Sun: 'sun',
  Cloud: 'cloud', CloudRain: 'cloud-rain', CloudDrizzle: 'cloud-drizzle', CloudLightning: 'cloud-lightning',
  Snowflake: 'snowflake', Wind: 'wind', Droplet: 'droplet', Thermometer: 'thermometer', Clock: 'clock-3', Location: 'map-pin',
  Calendar: 'calendar-days', Reminder: 'alarm-clock', Timer: 'timer', Camera: 'camera', Tools: 'wrench',
  Warning: 'triangle-alert', Error: 'circle-alert', Success: 'circle-check', Retry: 'rotate-cw',
  Help: 'circle-help', Information: 'info', Offline: 'cloud-off', Audio: 'music', Delete: 'trash-2',
  Visibility: 'eye', Power: 'zap', List: 'list', File: 'file-text', Edit: 'pen-line',
  Calculator: 'calculator', Image: 'image', Navigation: 'navigation', Globe: 'globe-2',
  WifiLow: 'wifi-low', WifiMedium: 'wifi-high', VisibilityOff: 'eye-off', Sparkles: 'sparkles',
  ChevronLeft: 'chevron-left', Check: 'check', Plus: 'plus',
  Languages: 'languages', Notebook: 'notebook-pen', Sunrise: 'sunrise', Smartphone: 'smartphone',
  Phone: 'phone', PhoneOff: 'phone-off', Download: 'download', BellRing: 'bell-ring', Moon: 'moon',
  Hand: 'hand', Flip: 'flip-vertical', Ear: 'ear', Swap: 'arrow-left-right',
};

const lordiconCandidates = {
  Wifi: 'https://lordicon.com/icons/system/outline/64-wifi',
  Microphone: 'https://lordicon.com/icons/system/outline/188-microphone',
  Search: 'https://lordicon.com/icons/system/outline/19-magnifier',
  Home: 'https://lordicon.com/icons/system/outline/63-home',
  Inbox: 'https://lordicon.com/icons/system/outline/361-inbox',
  Warning: 'https://lordicon.com/icons/system/outline/1140-warning-triangle',
  Calendar: 'https://lordicon.com/icons/system/outline/364-calendar-dots',
  Message: 'https://lordicon.com/icons/system/outline/4198-message',
  Send: 'https://lordicon.com/icons/system/outline/4214-send-horizontal',
  Settings: 'https://lordicon.com/icons/system/outline/39-cog',
  Audio: 'https://lordicon.com/icons/system/outline/1041-speaker',
  Location: 'https://lordicon.com/icons/system/outline/18-location-pin',
  Apps: 'https://lordicon.com/icons/system/outline/1388-grid-bento',
};

const names = [...new Set(Object.values(roleMap))].sort();
const cIdentifier = (name) => name.replaceAll('-', '_');
const arrays = [];
const declarations = [];

for (const name of names) {
  const source = path.join(sourceDir, `${name}.svg`);
  if (!fs.existsSync(source)) throw new Error(`Missing Lucide source: ${source}`);
  const svg = fs.readFileSync(source, 'utf8').replaceAll('currentColor', '#ffffff');
  const renderer = new Resvg(svg, { fitTo: { mode: 'width', value: size } });
  const png = PNG.sync.read(renderer.render().asPng());
  if (png.width !== size || png.height !== size) throw new Error(`${name}: expected ${size}x${size}, got ${png.width}x${png.height}`);
  const alpha = [];
  for (let index = 0; index < png.data.length; index += 4) alpha.push(png.data[index + 3]);
  const id = `aura_icon_${cIdentifier(name)}`;
  const rows = [];
  for (let index = 0; index < alpha.length; index += 16) {
    rows.push(`    ${alpha.slice(index, index + 16).map((value) => `0x${value.toString(16).padStart(2, '0').toUpperCase()}`).join(', ')},`);
  }
  arrays.push(`static const uint8_t ${id}_map[] = {\n${rows.join('\n')}\n};\n\nconst lv_image_dsc_t ${id} = {\n    .header = {\n        .magic = LV_IMAGE_HEADER_MAGIC,\n        .cf = LV_COLOR_FORMAT_A8,\n        .flags = 0,\n        .w = ${size},\n        .h = ${size},\n        .stride = ${size},\n        .reserved_2 = 0,\n    },\n    .data_size = sizeof(${id}_map),\n    .data = ${id}_map,\n    .reserved = nullptr,\n    .reserved_2 = nullptr,\n};`);
  declarations.push(`LV_IMAGE_DECLARE(${id});`);
}

fs.writeFileSync(path.join(outputDir, 'aura_icon_assets.hpp'), `#pragma once\n\n#include "lvgl.h"\n\n${declarations.join('\n')}\n`);
fs.writeFileSync(path.join(outputDir, 'aura_icon_assets.cpp'), `#include "aura_icon_assets.hpp"\n\n${arrays.join('\n\n')}\n`);
fs.writeFileSync(path.join(outputDir, 'manifest.json'), `${JSON.stringify({
  schema: 'aura.icon-registry.v12',
  generatedAt: '2026-10-22',
  renderedSize: size,
  activeSource: { family: 'Lucide', version: '1.59.0', license: 'ISC', reason: 'Lawful fallback: no user-provided Lordicon asset export or PRO entitlement; free Lordicon assets must not be scraped or modified.' },
  lordiconLicense: 'https://lordicon.com/licenses',
  roles: Object.entries(roleMap).map(([role, source]) => ({ role, source: `${source}.svg`, lordiconCandidate: lordiconCandidates[role] ?? null })),
}, null, 2)}\n`);

console.log(`Generated ${names.length} unique A8 assets for ${Object.keys(roleMap).length} semantic roles.`);
