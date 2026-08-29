// えんがわポスト — 依存ゼロのNodeサーバ (Node 18+)
// 起動: ANTHROPIC_API_KEY=sk-... node server.js   (キー無しならモックで動作)
const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = process.env.PORT || 8787;
const KEY = process.env.ANTHROPIC_API_KEY || '';
const MODEL = process.env.MODEL || 'claude-sonnet-4-5';

let latestLetter = null; // {text, photo, ts}
let latestReply = null;  // {photo, ts}

const MIME = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.css': 'text/css', '.png': 'image/png', '.jpg': 'image/jpeg', '.svg': 'image/svg+xml' };

function json(res, code, obj) {
  res.writeHead(code, { 'content-type': 'application/json' });
  res.end(JSON.stringify(obj));
}
async function readBody(req) {
  let b = '';
  for await (const c of req) b += c;
  return b ? JSON.parse(b) : {};
}

const server = http.createServer(async (req, res) => {
  try {
    const u = new URL(req.url, 'http://localhost');

    if (u.pathname === '/api/health') return json(res, 200, { ok: true, hasKey: !!KEY, model: MODEL });

    // Claude proxy(キーはサーバ側にのみ置く)
    if (u.pathname === '/api/chat' && req.method === 'POST') {
      const { messages, system, maxTokens } = await readBody(req);
      if (!KEY) return json(res, 200, { mock: true });
      const r = await fetch('https://api.anthropic.com/v1/messages', {
        method: 'POST',
        headers: { 'content-type': 'application/json', 'x-api-key': KEY, 'anthropic-version': '2023-06-01' },
        body: JSON.stringify({ model: MODEL, max_tokens: maxTokens || 400, system, messages })
      });
      const j = await r.json();
      if (!r.ok) return json(res, 502, { error: j });
      return json(res, 200, { text: j.content.map(c => c.text || '').join('') });
    }

    // お便り(孫→祖母)。スタックチャン等の物理端末も GET /api/letter をポーリングするだけで受信できる
    if (u.pathname === '/api/letter/plain') {
      const since = u.searchParams.get('since') || '0';
      res.writeHead(200, { 'content-type': 'text/plain; charset=utf-8' });
      if (latestLetter && String(latestLetter.ts) !== since) return res.end(latestLetter.ts + '\n' + latestLetter.text);
      return res.end('');
    }
    if (u.pathname === '/api/letter' && req.method === 'POST') {
      latestLetter = await readBody(req);
      latestLetter.ts = Date.now();
      return json(res, 200, { ok: true, ts: latestLetter.ts });
    }
    if (u.pathname === '/api/letter') {
      const since = Number(u.searchParams.get('since') || 0);
      return json(res, 200, { letter: latestLetter && latestLetter.ts > since ? latestLetter : null });
    }

    // スタックチャン(CoreS3)からの生JPEGお返事
    if (u.pathname === '/api/reply/raw' && req.method === 'POST') {
      const chunks = [];
      for await (const c of req) chunks.push(c);
      const buf = Buffer.concat(chunks);
      latestReply = { photo: 'data:image/jpeg;base64,' + buf.toString('base64'), ts: Date.now(), from: 'stackchan' };
      return json(res, 200, { ok: true });
    }

    // お返事(祖母→孫)
    if (u.pathname === '/api/reply' && req.method === 'POST') {
      latestReply = await readBody(req);
      latestReply.ts = Date.now();
      return json(res, 200, { ok: true, ts: latestReply.ts });
    }
    if (u.pathname === '/api/reply') {
      const since = Number(u.searchParams.get('since') || 0);
      return json(res, 200, { reply: latestReply && latestReply.ts > since ? latestReply : null });
    }

    // static
    const p = u.pathname === '/' ? '/index.html' : u.pathname;
    const safe = path.normalize(p).replace(/^(\.\.[\/\\])+/, '');
    const f = path.join(__dirname, 'public', safe);
    if (fs.existsSync(f) && fs.statSync(f).isFile()) {
      res.writeHead(200, { 'content-type': MIME[path.extname(f)] || 'application/octet-stream' });
      return fs.createReadStream(f).pipe(res);
    }
    res.writeHead(404); res.end('not found');
  } catch (e) {
    json(res, 500, { error: String(e) });
  }
});

server.listen(PORT, () => {
  console.log('えんがわポスト  書く側    → http://localhost:' + PORT + '/');
  console.log('              おばあちゃん側 → http://localhost:' + PORT + '/grandma.html');
  console.log(KEY ? 'Claude API: 接続 (' + MODEL + ')' : 'Claude API: キー未設定 → モックモード');
});
