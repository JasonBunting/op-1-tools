// op1inventory: serves index.html on http://localhost:8461/ and the Hundekraft workshop's own files under /ws/, so the
// page can import the workshop's op1push.js (its USB MIDI protocol) unchanged. Web MIDI needs a secure context, and
// localhost counts. Usage: node research/op1inventory/serve.mjs [path to op1-workshop]  (default D:\src\op1-workshop)
// Copyright (C) 2026 Jason Bunting. MIT.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const workshop = path.resolve(process.argv[2] || 'D:\\src\\op1-workshop');
const port = 8461;
const types = {'.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.json': 'application/json'};

if (!fs.existsSync(path.join(workshop, 'op1push.js'))) {
  console.error(`No op1push.js in ${workshop}. Pass the op1-workshop folder as the first argument.`);
  process.exit(1);
}
http.createServer((req, res) => {
  const url = decodeURIComponent(new URL(req.url, 'http://localhost').pathname);
  const [root, rel] = url.startsWith('/ws/') ? [workshop, url.slice(4)] : [here, url === '/' ? 'index.html' : url.slice(1)];
  const file = path.resolve(root, rel);
  if (!file.startsWith(root + path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
    res.writeHead(404); res.end('not found'); return;
  }
  res.writeHead(200, {'Content-Type': types[path.extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-store'});
  fs.createReadStream(file).pipe(res);
}).listen(port, '127.0.0.1', () => console.log(`op1inventory: http://localhost:${port}/  (workshop: ${workshop})`));
