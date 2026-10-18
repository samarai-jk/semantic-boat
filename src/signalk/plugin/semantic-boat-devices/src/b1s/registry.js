'use strict';

// The B1S registry (src/lib/b1s/registry.yaml): every number B1S assigns.
// The plugin reads it at start, so a new quantity needs no plugin change.

const fs = require('node:fs');
const path = require('node:path');
const yaml = require('js-yaml');

// Installed as a link to the repository (README), the plugin is a
// symlink and __dirname is its place in the repository.
const defaultRegistryPath = path.resolve(__dirname, '../../../../../lib/b1s/registry.yaml');

function indexRegistry(registry) {
  return {
    manufacturerCode: registry.manufacturer_code,
    industryCode: registry.industry_code,
    pgns: Object.fromEntries(registry.pgns.map(entry => [entry.id, entry.pgn])),
    quantities: new Map(registry.quantities.map(entry => [entry.code, entry])),
    products: new Map(registry.products.map(entry => [entry.product_code, entry])),
    services: Object.fromEntries(registry.services.map(entry => [entry.id, entry.code])),
    commands: Object.fromEntries(registry.commands.map(entry => [entry.id, entry.code])),
    parameters: new Map(registry.parameters.map(entry => [entry.code, entry])),
    parameterNames: new Map(registry.parameters.map(entry => [entry.id, entry]))
  };
}

function loadRegistry(file = defaultRegistryPath) {
  return indexRegistry(yaml.load(fs.readFileSync(file, 'utf8')));
}

module.exports = { defaultRegistryPath, indexRegistry, loadRegistry };
