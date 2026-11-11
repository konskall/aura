// 64 px A8 icons as LVGL images, stored compressed with LVGL's RLE (lv_rle.c;
// LV_USE_RLE in simulator/config/lv_conf.h and firmware/sdkconfig.defaults).
// Uncompressed, the four icon families took 1.6 MB of the firmware's flash.

// LVGL's RLE with one-byte blocks: a control byte below 0x80 repeats the next
// byte that many times; 0x80 | n copies the n bytes after it.
export function rleCompress(bytes) {
  const out = [];
  let index = 0;
  while (index < bytes.length) {
    let run = 1;
    while (index + run < bytes.length && run < 127 && bytes[index + run] === bytes[index]) run += 1;
    if (run >= 2) {
      out.push(run, bytes[index]);
      index += run;
      continue;
    }
    // Literals until a run of three begins or 127 are gathered.
    const start = index;
    while (index < bytes.length && index - start < 127) {
      if (index + 2 < bytes.length && bytes[index] === bytes[index + 1] && bytes[index] === bytes[index + 2]) break;
      index += 1;
    }
    out.push(0x80 | (index - start), ...bytes.slice(start, index));
  }
  return Uint8Array.from(out);
}

// The inverse, as lv_rle_decompress reads it; throws on anything it would refuse.
export function rleDecompress(input, size) {
  const out = [];
  let index = 0;
  while (index < input.length) {
    const control = input[index++];
    if (control & 0x80) {
      const count = control & 0x7f;
      if (index + count > input.length) throw new Error('RLE literal past the end');
      for (let copied = 0; copied < count; copied += 1) out.push(input[index++]);
    } else {
      if (index >= input.length) throw new Error('RLE run past the end');
      const value = input[index++];
      for (let repeated = 0; repeated < control; repeated += 1) out.push(value);
    }
  }
  if (out.length !== size) throw new Error(`RLE gives ${out.length} bytes, not ${size}`);
  return out;
}

const LV_IMAGE_COMPRESS_RLE = 1;

// What LVGL's bin decoder expects of a compressed in-memory image: a 12-byte
// lv_image_compressed_t (method, compressed size, decompressed size, all
// little-endian), then the compressed bytes.
export function compressedImageData(alpha) {
  const packed = rleCompress(alpha);
  const back = rleDecompress(packed, alpha.length);
  if (back.some((value, index) => value !== alpha[index])) throw new Error('RLE round trip changed a pixel');
  const header = Buffer.alloc(12);
  header.writeUInt32LE(LV_IMAGE_COMPRESS_RLE, 0);
  header.writeUInt32LE(packed.length, 4);
  header.writeUInt32LE(alpha.length, 8);
  return Buffer.concat([header, Buffer.from(packed)]);
}

const hex = (value) => `0x${value.toString(16).padStart(2, '0').toUpperCase()}`;

// One icon's C++: its compressed bytes and its descriptor, exported or static.
export function a8ImageSource({ id, alpha, size, exported }) {
  if (alpha.length !== size * size) throw new Error(`${id}: ${alpha.length} pixels, not ${size * size}`);
  const data = compressedImageData(alpha);
  const rows = [];
  for (let index = 0; index < data.length; index += 16) {
    rows.push(`    ${[...data.subarray(index, index + 16)].map(hex).join(', ')},`);
  }
  return `static const uint8_t ${id}_map[] = {\n${rows.join('\n')}\n};\n\n${exported ? '' : 'static '}`
    + `const lv_image_dsc_t ${id} = {\n    .header = {\n        .magic = LV_IMAGE_HEADER_MAGIC,\n`
    + `        .cf = LV_COLOR_FORMAT_A8,\n        .flags = LV_IMAGE_FLAGS_COMPRESSED,\n        .w = ${size},\n`
    + `        .h = ${size},\n        .stride = ${size},\n        .reserved_2 = 0,\n    },\n`
    + `    .data_size = sizeof(${id}_map),\n    .data = ${id}_map,\n    .reserved = nullptr,\n`
    + '    .reserved_2 = nullptr,\n};';
}
