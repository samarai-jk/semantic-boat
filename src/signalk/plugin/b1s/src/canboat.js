'use strict';

// canboat definitions of the B1S messages (layouts: src/lib/b1s/README.md).
// Registered with Signal K's canboatjs as 'canboat-custom-pgns', they make
// canboat decode B1S; the manufacturer and industry code fields must match,
// so other manufacturers' proprietary messages keep their own definitions.
// Values stay raw (resolution 1): their scale depends on the quantity.

function header(registry) {
  return [
    { Id: 'manufacturerCode', Name: 'Manufacturer Code', BitLength: 11, BitOffset: 0, BitStart: 0,
      Match: registry.manufacturerCode, Resolution: 1, Signed: false, FieldType: 'NUMBER' },
    { Id: 'reserved', Name: 'Reserved', BitLength: 2, BitOffset: 11, BitStart: 3, Resolution: 1,
      FieldType: 'RESERVED' },
    { Id: 'industryCode', Name: 'Industry Code', BitLength: 3, BitOffset: 13, BitStart: 5,
      Match: registry.industryCode, Resolution: 1, Signed: false, FieldType: 'NUMBER' }
  ];
}

function number(id, name, bitLength, bitOffset, extra = {}) {
  return { Id: id, Name: name, BitLength: bitLength, BitOffset: bitOffset, BitStart: bitOffset % 8,
    Resolution: 1, Signed: false, FieldType: 'NUMBER', ...extra };
}

function definition(pgn, id, description, type, length, fields) {
  return {
    PGN: pgn, Id: id, Description: description, Type: type, Complete: true, Length: length,
    FieldCount: fields.length,
    Fields: fields.map((field, index) => ({ Order: index + 1, ...field }))
  };
}

function canboatDefinitions(registry) {
  return {
    PGNs: [
      definition(registry.pgns.measurement, 'b1sMeasurement', 'B1S: Measurement', 'Single', 8, [
        ...header(registry),
        number('quantity', 'Quantity', 8, 16),
        number('channel', 'Channel', 8, 24),
        number('value', 'Value', 32, 32, { Signed: true })
      ]),
      definition(registry.pgns.channelDescription, 'b1sChannelDescription', 'B1S: Channel Description', 'Fast', 76, [
        ...header(registry),
        number('channel', 'Channel', 8, 16),
        number('kind', 'Quantity or Output Kind', 8, 24),
        number('encoding', 'Encoding', 2, 32),
        number('output', 'Output Channel', 1, 34),
        number('noStandard', 'No Standard PGN', 1, 35),
        { Id: 'reserved2', Name: 'Reserved', BitLength: 4, BitOffset: 36, BitStart: 4, Resolution: 1,
          FieldType: 'RESERVED' },
        number('standardInstance', 'Standard PGN Instance', 8, 40),
        number('period', 'Nominal Period', 16, 48, { Resolution: 0.01, Unit: 's' }),
        { Id: 'location', Name: 'Location', BitLengthVariable: true, BitOffset: 64, BitStart: 0,
          FieldType: 'STRING_LAU' },
        { Id: 'label', Name: 'Label', BitLengthVariable: true, FieldType: 'STRING_LAU' }
      ])
    ]
  };
}

module.exports = { canboatDefinitions };
