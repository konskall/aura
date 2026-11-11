// Rewrites the generated icon files with their images RLE-compressed, the
// pixels unchanged: the generators now write that form themselves
// (lvgl-a8-image.mjs), and this brings the files they wrote before up to it
// without rendering the icons again. An image already compressed is left as
// it is. Run from the repository root: node tools/icons/compress-icon-assets.mjs
import fs from 'node:fs';
import path from 'node:path';
import { a8ImageSource } from './lvgl-a8-image.mjs';

const size = 64;
const directory = path.join(process.cwd(), 'shared', 'assets', 'icons');
const files = ['aura_icon_assets.cpp', 'aura_icon_assets_material.cpp', 'aura_icon_assets_phosphor.cpp',
  'aura_icon_assets_solar.cpp'];
const block = /static const uint8_t (\w+)_map\[\] = \{\n([\s\S]*?)\n\};\n\n(static )?const lv_image_dsc_t \1 = \{\n[\s\S]*?\n\};/g;

for (const name of files) {
  const file = path.join(directory, name);
  const before = fs.readFileSync(file, 'utf8');
  let images = 0;
  let rawBytes = 0;
  let storedBytes = 0;
  const after = before.replace(block, (whole, id, body, isStatic) => {
    if (whole.includes('LV_IMAGE_FLAGS_COMPRESSED')) return whole;
    const alpha = [...body.matchAll(/0x([0-9A-Fa-f]{2})/g)].map((match) => Number.parseInt(match[1], 16));
    const source = a8ImageSource({ id, alpha, size, exported: !isStatic });
    images += 1;
    rawBytes += alpha.length;
    storedBytes += [...source.matchAll(/0x[0-9A-F]{2}/g)].length;
    return source;
  });
  if (after !== before) fs.writeFileSync(file, after);
  console.log(`${name}: ${images} images compressed, ${rawBytes} -> ${storedBytes} bytes`);
}
