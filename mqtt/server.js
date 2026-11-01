// CS460 phone-sensor MQTT bridge — one Node process, one container.
//
//   * MQTT over TCP   :1883   — the TBD twin subscribes here
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
const crypto = require('crypto');
const ws = require('websocket-stream');
const qrcode = require('qrcode');

const TCP_PORT = parseInt(process.env.MQTT_TCP_PORT || '1883', 10);
const HTTP_PORT = parseInt(process.env.HTTP_PORT || '8090', 10);
const PAGE = fs.readFileSync(path.join(__dirname, 'public', 'index.html'));

// Per-session access key. The page and the WebSocket are both gated on ?k=<KEY>, so a
// bare guess of the public Codespace URL cannot load the sharing UI or subscribe to
// phone/# — only someone who scanned THIS session's QR (which carries the key) can.
// The twin on local TCP :1883 is not gated (it never leaves the container).
const KEY = process.env.BRIDGE_KEY || crypto.randomBytes(4).toString('hex');
try { fs.writeFileSync('/tmp/bridge.key', KEY); } catch (e) {}

// ---- MQTT over TCP (the twin connects here) ----
net.createServer(aedes.handle).listen(TCP_PORT, () =>
  console.log(`[bridge] MQTT/TCP   :${TCP_PORT}  (TBD twin subscribes here)`));

// ---- HTTP: the sensor page + the dynamic QR ----
function publicUrl(req) {
  const host = req.headers['x-forwarded-host'] || req.headers.host || `localhost:${HTTP_PORT}`;
  const proto = req.headers['x-forwarded-proto'] || (host.includes('app.github.dev') ? 'https' : 'http');
  return `${proto}://${host}/?k=${KEY}`;   // the key travels with the QR / link
}
function keyOf(req) { try { return new URL(req.url, 'http://x').searchParams.get('k'); } catch (e) { return null; } }
const DENY = Buffer.from('<!doctype html><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">'
  + '<body style="font:16px system-ui;max-width:34rem;margin:12vh auto;padding:0 1.2rem;background:#0b0e14;color:#e7eef6">'
  + '<h2>This link needs its session key</h2><p>Open the phone page using the QR code or link printed by '
  + '<code>bash mqtt/share.sh</code> in the Codespace. That link carries a one-time key for this session, so a '
  + 'guessed URL can’t load the page or read anyone’s sensors.</p>');
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
  if (keyOf(req) !== KEY) {          // bare/guessed URL: no sharing UI, no key leaked
    res.writeHead(403, { 'Content-Type': 'text/html; charset=utf-8' });
    return res.end(DENY);
  }
  res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
  res.end(PAGE);
});

// ---- MQTT over WebSocket on the SAME http server (phones publish here) ----
// Gate the WS handshake on the same key, so only a client that loaded the keyed page
// can connect to publish OR subscribe (phone/#).
ws.createServer({ server: httpServer, verifyClient: (info, cb) => cb(keyOf(info.req) === KEY) }, aedes.handle);
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
console.log(`[bridge] session key: ${KEY}  (page + WS require ?k=${KEY}; 'bash mqtt/share.sh' prints the full link/QR)`);
