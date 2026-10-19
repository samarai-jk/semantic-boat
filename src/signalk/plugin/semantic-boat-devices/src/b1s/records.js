'use strict';

// Record parameters (registry `records`): fixed little-endian layouts of
// uint8, uint16, int32, enum (uint8 with named values), and flags (uint8,
// bit n = the n-th name). Decoded as { field: number | enum name | [flag names] }.

const sizes = { uint8: 1, enum: 1, flags: 1, uint16: 2, int32: 4 };

function decodeRecord(layout, bytes) {
  const fields = {};
  let offset = 0;
  for (const field of layout) {
    let value;
    if (field.type === 'int32') {
      value = (bytes[offset] | bytes[offset + 1] << 8 | bytes[offset + 2] << 16 | bytes[offset + 3] << 24) | 0;
    } else if (field.type === 'uint16') {
      value = bytes[offset] | bytes[offset + 1] << 8;
    } else {
      value = bytes[offset];
    }
    if (field.type === 'enum') value = field.values[value] ?? value;
    if (field.type === 'flags') value = field.bits.filter((bit, index) => value >> index & 1);
    fields[field.name] = value;
    offset += sizes[field.type];
  }
  return fields;
}

function encodeRecord(layout, fields) {
  const bytes = [];
  for (const field of layout) {
    let value = fields[field.name];
    if (value === undefined || value === null) throw new Error(`Record field ${field.name} is missing`);
    if (field.type === 'enum' && typeof value === 'string') {
      const index = field.values.indexOf(value);
      if (index < 0) throw new Error(`${field.name}: one of ${field.values.join(', ')}`);
      value = index;
    } else if (field.type === 'flags') {
      value = (Array.isArray(value) ? value : []).reduce((sum, bit) => {
        const index = field.bits.indexOf(bit);
        if (index < 0) throw new Error(`${field.name}: flags ${field.bits.join(', ')}`);
        return sum | 1 << index;
      }, 0);
    }
    value = Number(value);
    if (!Number.isInteger(value)) throw new Error(`${field.name} needs an integer`);
    for (let index = 0; index < sizes[field.type]; index += 1) bytes.push(value >> (8 * index) & 0xff);
  }
  return bytes;
}

module.exports = { decodeRecord, encodeRecord };
