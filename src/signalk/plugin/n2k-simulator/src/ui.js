'use strict';

const path = require('node:path');
const publicDirectory = path.join(__dirname, '../public');
const assets = {
  'index.html': [path.join(publicDirectory, 'index.html'), 'text/html'],
  'app.js': [path.join(publicDirectory, 'app.js'), 'text/javascript'],
  'style.css': [path.join(publicDirectory, 'style.css'), 'text/css'],
  'leaflet.js': [require.resolve('leaflet/dist/leaflet.js'), 'text/javascript'],
  'leaflet.css': [require.resolve('leaflet/dist/leaflet.css'), 'text/css'],
  'nouislider.js': [require.resolve('nouislider/dist/nouislider.js'), 'text/javascript'],
  'nouislider.css': [require.resolve('nouislider/dist/nouislider.css'), 'text/css'],
  'lucide.js': [require.resolve('lucide/dist/umd/lucide.js'), 'text/javascript']
};

function registerUi(router) {
  router.get('/ui', (request, response) => request.path.endsWith('/')
    ? response.sendFile(assets['index.html'][0]) : response.redirect('ui/'));
  for (const [name, [file, type]] of Object.entries(assets)) {
    router.get(`/ui/${name}`, (_request, response) => response.type(type).sendFile(file));
  }
}

module.exports = { assets, registerUi };