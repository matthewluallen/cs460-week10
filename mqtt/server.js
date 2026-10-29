// CS460 phone-sensor MQTT bridge — one Node process, one container.
//
//   * MQTT over TCP   :1883   — the M5StampLC twin subscribes here
//   * MQTT over WS    :8090   — phone browsers publish here (same origin as the page)
//   * HTTP            :8090   — serves the sensor page + a QR of this server's PUBLIC url
//
// No auth (lab). Phones reach the page over the Codespace public forward of :8090; the QR
// is generated from the request's forwarded host, so it always matches whatever URL the
// students actually reach — scan it and the phone opens the same page.
const aedes = require('aedes')();
const net = require('net');
const http = require('http');
const fs = require('fs');
const path = require('path');
const ws = require('websocket-stream');
const qrcode = require('qrcode');

const TCP_PORT = parseInt(process.env.MQTT_TCP_PORT || '1883', 10);
const HTTP_PORT = parseInt(process.env.HTTP_PORT || '8090', 10);
const PAGE = fs.readFileSync(path.join(__dirname, 'public', 'index.html'));

// ---- MQTT over TCP (the twin connects here) ----
net.createServer(aedes.handle).listen(TCP_PORT, () =>
  console.log(`[bridge] MQTT/TCP   :${TCP_PORT}  (M5StampLC twin subscribes here)`));

// ---- HTTP: the sensor page + the dynamic QR ----
function publicUrl(req) {
  const host = req.headers['x-forwarded-host'] || req.headers.host || `localhost:${HTTP_PORT}`;
  const proto = req.headers['x-forwarded-proto'] || (host.includes('app.github.dev') ? 'https' : 'http');
  return `${proto}://${host}/`;
}
const httpServer = http.createServer((req, res) => {
  const url = req.url.split('?')[0];
  if (url === '/qr.png') {
    qrcode.toBuffer(publicUrl(req), { width: 340, margin: 1, color: { dark: '#0b0e14', light: '#ffffff' } },
      (e, buf) => {
        if (e) { res.writeHead(500); return res.end('qr error'); }
        res.writeHead(200, { 'Content-Type': 'image/png', 'Cache-Control': 'no-store' });
        res.end(buf);
      });
    return;
  }
  if (url === '/whoami') {           // the page asks the server what its public url is
    res.writeHead(200, { 'Content-Type': 'application/json' });
    return res.end(JSON.stringify({ url: publicUrl(req) }));
  }
  res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
  res.end(PAGE);
});

// ---- MQTT over WebSocket on the SAME http server (phones publish here) ----
ws.createServer({ server: httpServer }, aedes.handle);
httpServer.listen(HTTP_PORT, () =>
  console.log(`[bridge] HTTP + MQTT/WS :${HTTP_PORT}  (phone sensor page + QR)`));

// ---- Visibility: log + retain the latest value per sensor topic (for late subscribers) ----
const seen = new Map();
aedes.on('client', c => console.log(`[bridge] + client ${c.id}`));
aedes.on('clientDisconnect', c => console.log(`[bridge] - client ${c.id}`));
aedes.on('subscribe', (subs, c) =>
  console.log(`[bridge] ${c && c.id} SUBSCRIBE ${subs.map(s => s.topic).join(', ')}`));
aedes.on('publish', (pkt, c) => {
  if (!c || !pkt.topic.startsWith('phone/')) return;
  const was = seen.get(pkt.topic);
  seen.set(pkt.topic, Date.now());
  const s = pkt.payload.toString().slice(0, 90);
  if (!was || Date.now() - was > 2000) console.log(`[bridge] ${pkt.topic}  ${s}`);
});

console.log('[bridge] CS460 phone-sensor MQTT bridge up.');
