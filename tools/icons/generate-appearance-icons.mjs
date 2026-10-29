// Generates the icon families of the Marble, Aurora and Graphite appearances
// as 64 px A8 LVGL images, the same format as the Lucide set, plus one
// resolver per family. A role a family cannot draw faithfully returns nullptr
// and the UI falls back to Lucide (listed in each manifest).
// Run through tools/icons/build-icons.ps1 from the repository root.
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

const root = process.cwd();
const require = createRequire(path.join(root, '.tools', 'icon-build', 'package.json'));
const { Resvg } = require('@resvg/resvg-js');
const { PNG } = require('pngjs');
const outputDir = path.join(root, 'shared', 'assets', 'icons');
const size = 64;

// Candidate base names per role; the first that exists in the set wins.
// Roles missing here (or with no match) use the Lucide asset.
const families = {
  phosphor: {
    title: 'Phosphor Light', set: 'ph', license: 'MIT', symbol: 'ph',
    variants: () => (name) => [`${name}-light`],
    roles: {
      Home: ['house'], Back: ['arrow-left'], Forward: ['arrow-right'], Menu: ['list'], Close: ['x'],
      Apps: ['squares-four'], Chevron: ['caret-right'], Refresh: ['arrow-clockwise'], More: ['dots-three'],
      Microphone: ['microphone'], MicMuted: ['microphone-slash'], Chat: ['chat-circle'], Search: ['magnifying-glass'],
      Send: ['paper-plane-right'], Cancel: ['x-circle'], Thinking: ['brain'], Copy: ['copy'],
      Confirmation: ['check-circle'], Voice: ['waveform'], Play: ['play'], Pause: ['pause'], Stop: ['stop'],
      Speaker: ['speaker-high'], VolumeDown: ['speaker-low'], VolumeUp: ['speaker-high'], Muted: ['speaker-slash', 'speaker-x'],
      TestSound: ['wave-sine'], Record: ['record', 'circle'], Message: ['chat-centered-text'], Inbox: ['tray'],
      Unread: ['envelope'], Notification: ['bell'], Compose: ['note-pencil'], Reply: ['arrow-bend-up-left'],
      Room: ['users'], Lock: ['lock'], Encrypted: ['shield-check'], Connection: ['link'],
      Wifi: ['wifi-high'], WifiOff: ['wifi-slash'], Bluetooth: ['bluetooth'], Battery: ['battery-medium'],
      Charging: ['battery-charging'], Display: ['monitor'], Brightness: ['sun'], Settings: ['gear-six'],
      Storage: ['hard-drives'], Cpu: ['cpu'], Memory: ['memory'], Weather: ['cloud-sun'], Sun: ['sun'],
      Cloud: ['cloud'], CloudRain: ['cloud-rain'], CloudDrizzle: ['cloud-rain'], CloudLightning: ['cloud-lightning'],
      Snowflake: ['snowflake'], Wind: ['wind'], Droplet: ['drop'], Thermometer: ['thermometer-simple'],
      Clock: ['clock'], Location: ['map-pin'], Calendar: ['calendar-dots', 'calendar-blank'], Reminder: ['alarm'],
      Timer: ['timer'], Camera: ['camera'], Tools: ['wrench'], Warning: ['warning'], Error: ['warning-circle'],
      Success: ['check-circle'], Retry: ['arrow-clockwise'], Help: ['question'], Information: ['info'],
      Offline: ['cloud-slash'], Audio: ['music-notes'], Delete: ['trash'], Visibility: ['eye'], Power: ['lightning'],
      List: ['list-bullets'], File: ['file-text'], Edit: ['pencil-simple-line'], Calculator: ['calculator'],
      Image: ['image'], Navigation: ['navigation-arrow'], Globe: ['globe'], WifiLow: ['wifi-low'],
      WifiMedium: ['wifi-medium'], VisibilityOff: ['eye-slash'], Sparkles: ['sparkle'], ChevronLeft: ['caret-left'],
      Check: ['check'], Plus: ['plus'], Languages: ['translate'], Notebook: ['notebook'], Sunrise: ['sun-horizon'],
      Smartphone: ['device-mobile'], Phone: ['phone'], PhoneOff: ['phone-slash', 'phone-x'], Download: ['download-simple'],
      BellRing: ['bell-ringing'], Moon: ['moon'], Hand: ['hand'], Flip: ['flip-vertical'], Ear: ['ear'],
      Swap: ['arrows-left-right'], Palette: ['palette'],
    },
  },
  solar: {
    title: 'Solar Bold Duotone', set: 'solar', license: 'CC BY 4.0', symbol: 'solar',
    // Thin navigation glyphs read better as line icons; everything else is bold duotone.
    variants: (role) => {
      const line = new Set(['Back', 'Forward', 'Chevron', 'ChevronLeft', 'Close', 'Check', 'Plus', 'More', 'Menu']);
      return line.has(role) ? (name) => [`${name}-linear`, `${name}-line-duotone`] :
                              (name) => [`${name}-bold-duotone`, `${name}-bold`];
    },
    roles: {
      Home: ['home-2'], Back: ['alt-arrow-left'], Forward: ['alt-arrow-right'], Menu: ['hamburger-menu'],
      Close: ['close-circle'], Apps: ['widget-4', 'widget'], Chevron: ['alt-arrow-right'], Refresh: ['refresh'],
      More: ['menu-dots'], Microphone: ['microphone-3'], MicMuted: ['microphone-slash', 'muted'],
      Chat: ['chat-round-dots'], Search: ['magnifer'], Send: ['plain-2', 'plain'], Cancel: ['close-circle'],
      Thinking: ['lightbulb-bolt', 'lightbulb'], Copy: ['copy'], Confirmation: ['check-circle'], Voice: ['soundwave'],
      Play: ['play'], Pause: ['pause'], Stop: ['stop'], Speaker: ['volume-loud'], VolumeDown: ['volume-small'],
      VolumeUp: ['volume-loud'], Muted: ['volume-cross'], TestSound: ['music-notes', 'soundwave'],
      Record: ['record-circle', 'record'], Message: ['chat-square', 'chat-line'], Inbox: ['inbox'],
      Unread: ['letter-unread', 'letter'], Notification: ['bell'], Compose: ['pen-new-square'], Reply: ['reply'],
      Room: ['users-group-rounded'], Lock: ['lock-keyhole', 'lock'], Encrypted: ['shield-check'], Connection: ['link'],
      Wifi: ['wi-fi-high'], WifiOff: ['wi-fi-off'], Bluetooth: ['bluetooth'], Battery: ['battery-half', 'battery-full'],
      Charging: ['battery-charge'], Display: ['monitor'], Brightness: ['sun-2', 'sun'], Settings: ['settings'],
      Storage: ['server-square', 'server'], Cpu: ['cpu'], Memory: ['ssd-square', 'database'], Weather: ['cloud-sun-2'],
      Sun: ['sun'], Cloud: ['cloud'], CloudRain: ['cloud-rain'], CloudDrizzle: ['cloud-waterdrop', 'cloud-rain'],
      CloudLightning: ['cloud-bolt', 'cloud-storm'], Snowflake: ['snowflake'], Wind: ['wind'], Droplet: ['waterdrop'],
      Thermometer: ['temperature'], Clock: ['clock-circle'], Location: ['map-point'], Calendar: ['calendar'],
      Reminder: ['alarm'], Timer: ['stopwatch'], Camera: ['camera'], Tools: ['settings-minimalistic', 'tuning-2'],
      Warning: ['danger-triangle'], Error: ['danger-circle'], Success: ['check-circle'], Retry: ['refresh'],
      Help: ['question-circle'], Information: ['info-circle'], Offline: ['cloud-cross'], Audio: ['music-note'],
      Delete: ['trash-bin-trash', 'trash-bin-minimalistic'], Visibility: ['eye'], Power: ['bolt'],
      List: ['list'], File: ['document-text'], Edit: ['pen'], Calculator: ['calculator'], Image: ['gallery'],
      Navigation: ['map-arrow-up', 'routing-2'], Globe: ['globe'], WifiLow: ['wi-fi-low'], WifiMedium: ['wi-fi-high'],
      VisibilityOff: ['eye-closed'], Sparkles: ['stars'], ChevronLeft: ['alt-arrow-left'], Check: ['check-read', 'check'],
      Plus: ['add-circle', 'add-square'], Languages: ['dialog-2'], Notebook: ['notebook'], Sunrise: ['sunrise'],
      Smartphone: ['smartphone'], Phone: ['phone'], PhoneOff: ['end-call'], Download: ['download-minimalistic'],
      BellRing: ['bell-bing'], Moon: ['moon'], Hand: ['hand-shake', 'hand-stars'], Flip: ['flip-vertical'],
      Ear: ['headphones-round', 'ear'], Swap: ['transfer-horizontal'], Palette: ['palette'],
    },
  },
  material: {
    title: 'Material Symbols Rounded', set: 'material-symbols', license: 'Apache 2.0', symbol: 'ms',
    variants: () => (name) => [`${name}-rounded`, name],
    roles: {
      Home: ['home'], Back: ['arrow-back-ios-new', 'arrow-back'], Forward: ['arrow-forward-ios', 'arrow-forward'],
      Menu: ['menu'], Close: ['close'], Apps: ['apps'], Chevron: ['chevron-right'], Refresh: ['refresh'],
      More: ['more-horiz'], Microphone: ['mic'], MicMuted: ['mic-off'], Chat: ['chat-bubble'], Search: ['search'],
      Send: ['send'], Cancel: ['cancel'], Thinking: ['psychology'], Copy: ['content-copy'],
      Confirmation: ['check-circle'], Voice: ['graphic-eq'], Play: ['play-arrow'], Pause: ['pause'], Stop: ['stop'],
      Speaker: ['volume-up'], VolumeDown: ['volume-down'], VolumeUp: ['volume-up'], Muted: ['volume-off'],
      TestSound: ['music-note'], Record: ['radio-button-checked'], Message: ['chat'], Inbox: ['inbox'],
      Unread: ['mark-email-unread', 'mail'], Notification: ['notifications'], Compose: ['edit-square'],
      Reply: ['reply'], Room: ['group'], Lock: ['lock'], Encrypted: ['verified-user'], Connection: ['link'],
      Wifi: ['wifi'], WifiOff: ['wifi-off'], Bluetooth: ['bluetooth'], Battery: ['battery-horiz-050', 'battery-5-bar'],
      Charging: ['battery-charging-full'], Display: ['desktop-windows'], Brightness: ['brightness-medium'],
      Settings: ['settings'], Storage: ['hard-drive'], Cpu: ['memory'], Memory: ['memory-alt', 'memory'],
      Weather: ['partly-cloudy-day'], Sun: ['sunny'], Cloud: ['cloud'], CloudRain: ['rainy'],
      CloudDrizzle: ['rainy-light', 'rainy'], CloudLightning: ['thunderstorm'], Snowflake: ['ac-unit'],
      Wind: ['air'], Droplet: ['water-drop'], Thermometer: ['thermostat'], Clock: ['schedule'],
      Location: ['location-on'], Calendar: ['calendar-month'], Reminder: ['alarm'], Timer: ['timer'],
      Camera: ['photo-camera'], Tools: ['build'], Warning: ['warning'], Error: ['error'], Success: ['check-circle'],
      Retry: ['refresh'], Help: ['help'], Information: ['info'], Offline: ['cloud-off'], Audio: ['music-note'],
      Delete: ['delete'], Visibility: ['visibility'], Power: ['bolt'], List: ['list'], File: ['description'],
      Edit: ['edit'], Calculator: ['calculate'], Image: ['image'], Navigation: ['navigation'], Globe: ['language'],
      WifiLow: ['wifi-1-bar'], WifiMedium: ['wifi-2-bar'], VisibilityOff: ['visibility-off'],
      Sparkles: ['auto-awesome'], ChevronLeft: ['chevron-left'], Check: ['check'], Plus: ['add'],
      Languages: ['translate'], Notebook: ['sticky-note-2', 'edit-note'], Sunrise: ['wb-twilight'],
      Smartphone: ['smartphone'], Phone: ['call'], PhoneOff: ['call-end'], Download: ['download'],
      BellRing: ['notifications-active'], Moon: ['dark-mode', 'bedtime'], Hand: ['back-hand', 'pan-tool'],
      Flip: ['flip'], Ear: ['hearing'], Swap: ['swap-horiz'], Palette: ['palette'],
    },
  },
};

const cIdentifier = (name) => name.replaceAll('-', '_');
const load = (set) => {
  const base = path.join(root, '.tools', 'icon-build', 'node_modules', '@iconify-json', set);
  return { icons: JSON.parse(fs.readFileSync(path.join(base, 'icons.json'), 'utf8')),
           info: JSON.parse(fs.readFileSync(path.join(base, 'info.json'), 'utf8')) };
};
const resolve = (data, name) => {
  if (data.icons[name]) return data.icons[name];
  const alias = data.aliases && data.aliases[name];
  return alias && data.icons[alias.parent] ? { ...data.icons[alias.parent], ...alias } : null;
};

for (const [family, config] of Object.entries(families)) {
  const { icons: data, info } = load(config.set);
  const chosen = {};
  const fallbacks = [];
  for (const [role, candidates] of Object.entries(config.roles)) {
    const variant = config.variants(role);
    let hit = null;
    for (const base of candidates) {
      for (const name of variant(base)) {
        const icon = resolve(data, name);
        if (icon) { hit = { name, icon }; break; }
      }
      if (hit) break;
    }
    if (hit) chosen[role] = hit;
    else fallbacks.push(role);
  }
  const arrays = [];
  const declared = new Set();
  for (const { name, icon } of Object.values(chosen)) {
    if (declared.has(name)) continue;
    declared.add(name);
    const width = icon.width || data.width || 24;
    const height = icon.height || data.height || 24;
    const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${icon.left || 0} ${icon.top || 0} ${width} ${height}">${icon.body}</svg>`
      .replaceAll('currentColor', '#ffffff');
    const png = PNG.sync.read(new Resvg(svg, { fitTo: { mode: 'width', value: size } }).render().asPng());
    if (png.width !== size || png.height !== size) throw new Error(`${family}/${name}: ${png.width}x${png.height}`);
    const alpha = [];
    for (let index = 0; index < png.data.length; index += 4) alpha.push(png.data[index + 3]);
    const id = `aura_icon_${config.symbol}_${cIdentifier(name)}`;
    const rows = [];
    for (let index = 0; index < alpha.length; index += 16) {
      rows.push(`    ${alpha.slice(index, index + 16).map((value) => `0x${value.toString(16).padStart(2, '0').toUpperCase()}`).join(', ')},`);
    }
    arrays.push(`static const uint8_t ${id}_map[] = {\n${rows.join('\n')}\n};\n\nstatic const lv_image_dsc_t ${id} = {\n    .header = {\n        .magic = LV_IMAGE_HEADER_MAGIC,\n        .cf = LV_COLOR_FORMAT_A8,\n        .flags = 0,\n        .w = ${size},\n        .h = ${size},\n        .stride = ${size},\n        .reserved_2 = 0,\n    },\n    .data_size = sizeof(${id}_map),\n    .data = ${id}_map,\n    .reserved = nullptr,\n    .reserved_2 = nullptr,\n};`);
  }
  const cases = Object.entries(chosen)
    .map(([role, { name }]) => `        case aura::ui::IconRole::${role}: return &aura_icon_${config.symbol}_${cIdentifier(name)};`)
    .join('\n');
  const fn = `aura_icon_${family}`;
  fs.writeFileSync(path.join(outputDir, `aura_icon_assets_${family}.hpp`),
    `#pragma once\n\n#include "aura/aura_icons.hpp"\n#include "lvgl.h"\n\n// ${config.title} (${config.license}). nullptr = use the Lucide asset.\nconst lv_image_dsc_t* ${fn}(aura::ui::IconRole role) noexcept;\n`);
  fs.writeFileSync(path.join(outputDir, `aura_icon_assets_${family}.cpp`),
    `// Generated by tools/icons/generate-appearance-icons.mjs; do not edit.\n#include "aura_icon_assets_${family}.hpp"\n\n${arrays.join('\n\n')}\n\nconst lv_image_dsc_t* ${fn}(aura::ui::IconRole role) noexcept {\n    switch (role) {\n${cases}\n        default: break;\n    }\n    return nullptr;\n}\n`);
  fs.writeFileSync(path.join(outputDir, `manifest-${family}.json`), `${JSON.stringify({
    schema: 'aura.icon-family.v1', family: config.title, iconifySet: config.set, version: info.version ?? null,
    license: info.license?.title ?? config.license, author: info.author?.name ?? null, renderedSize: size,
    roles: Object.fromEntries(Object.entries(chosen).map(([role, { name }]) => [role, name])),
    lucideFallbacks: fallbacks,
  }, null, 2)}\n`);
  console.log(`${config.title}: ${declared.size} assets for ${Object.keys(chosen).length} roles; Lucide fallbacks: ${fallbacks.join(', ') || 'none'}`);
}
