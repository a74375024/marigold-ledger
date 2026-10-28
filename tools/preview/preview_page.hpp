// The page marigold-preview writes: the board's data goes in place of
// __DATA__. One file, nothing loaded but the team logos and flags.
#pragma once

namespace preview {

constexpr const char* kPage = R"PAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Live Events preview</title>
<style>
:root{--bg:#0e1015;--panel:#1c1f25;--tile:#1c1f25;--tile2:#262a32;--ink:#fff;--sub:rgba(255,255,255,.62);--faint:rgba(255,255,255,.4);--live:#C62828;--line:rgba(255,255,255,.08)}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.35 -apple-system,BlinkMacSystemFont,Inter,system-ui,sans-serif}
header{position:sticky;top:0;z-index:5;background:rgba(14,16,21,.96);border-bottom:1px solid var(--line);padding:14px 28px 12px}
h1{font-size:22px;margin:0 0 4px}
.meta{color:var(--sub);font-size:13px}
.bar{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-top:10px}
.chip{border:0;border-radius:15px;padding:5px 12px;background:rgba(255,255,255,.1);color:var(--ink);font:600 13px/1 inherit;cursor:pointer}
.chip.on{background:#fff;color:#000}
.chip .dot{display:inline-block;width:7px;height:7px;border-radius:4px;background:var(--live);margin-left:6px;vertical-align:1px}
input[type=search]{background:rgba(255,255,255,.1);border:0;border-radius:8px;color:var(--ink);padding:7px 10px;font:14px inherit;width:220px}
label.opt{color:var(--sub);font-size:13px;display:flex;gap:6px;align-items:center}
main{padding:10px 0 60px}
section.row{margin:22px 0 0}
section.row h2{font-size:20px;margin:0 28px 8px;display:flex;gap:10px;align-items:baseline}
section.row h2 small{color:var(--sub);font-weight:500;font-size:13px}
.strip{display:flex;gap:22px;overflow-x:auto;padding:4px 28px 10px;scrollbar-width:thin}
.group{flex:none}
.group .gt{font-size:12px;font-weight:700;letter-spacing:1px;color:var(--sub);margin:0 0 8px;text-transform:uppercase}
.tiles{display:flex;gap:14px}
.tile{position:relative;width:300px;height:176px;border-radius:14px;background:var(--tile);padding:12px 14px;display:flex;flex-direction:column;gap:7px;cursor:pointer;flex:none;overflow:hidden}
.tile:hover{outline:3px solid #fff;background:var(--tile2)}
.tile.sel{outline:3px solid #7fb2ff}
.tile.dim{opacity:.45}
.tile .top{display:flex;align-items:center;gap:8px;font-size:11px;font-weight:700;letter-spacing:.6px;color:var(--sub);text-transform:uppercase;height:14px}
.tile .top .tag.alert{color:#ffb74d}
.tile .top .num{margin-left:auto;color:var(--faint);font-weight:600;letter-spacing:0}
.side{display:flex;align-items:center;gap:10px;font-size:17px;font-weight:700;min-height:30px}
.side .nm{white-space:nowrap;overflow:hidden;text-overflow:ellipsis;flex:1}
.side .rk{font-size:11px;color:var(--sub);font-weight:600;margin-right:-4px}
.side .sc{font-size:20px;min-width:24px;text-align:right}
.side.trails{color:var(--sub)}
.side .ball{width:7px;height:7px;border-radius:4px;background:#ffd54f;display:inline-block}
.badge{width:30px;height:30px;border-radius:15px;display:flex;align-items:center;justify-content:center;font-size:10px;font-weight:800;flex:none;overflow:hidden}
.badge img{width:26px;height:26px;object-fit:contain}
.badge.plain{background:rgba(255,255,255,.14)!important;box-shadow:none!important}
.flags{display:flex;gap:2px;flex:none}
.flags img{width:22px;height:15px;object-fit:cover;border-radius:2px}
.cc{font-size:10px;font-weight:800;background:rgba(255,255,255,.14);border-radius:4px;padding:2px 4px;flex:none}
.ttl{font-size:17px;font-weight:700;display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical;overflow:hidden}
.subt{font-size:13px;color:var(--sub);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.sym{font-size:22px;margin-right:6px}
.ico{display:inline-block;width:22px;height:22px;margin-right:8px;vertical-align:-4px}
.ico svg{width:22px;height:22px}
.lead{display:flex;gap:8px;font-size:14px}
.lead b{width:26px;color:var(--sub)}
.lead span:last-child{margin-left:auto}
.parts{display:flex;gap:4px}
.parts div{flex:1;height:5px;border-radius:3px;background:rgba(255,255,255,.18);overflow:hidden}
.parts div i{display:block;height:5px;background:#fff}
.parts div.cur{outline:1px solid #fff}
.podium{display:flex;gap:6px}
.podium span{font-size:12px;font-weight:800;padding:2px 6px;border-radius:4px}
.foot{margin-top:auto;display:flex;align-items:center;gap:8px;font-size:13px;color:var(--sub);white-space:nowrap;overflow:hidden}
.foot .st{color:var(--ink);font-weight:600}
.foot .st.bold{font-weight:800}
.pill{font-size:11px;font-weight:800;padding:2px 6px;border-radius:5px;letter-spacing:.3px;flex:none}
.pill.live{background:var(--live);color:#fff}
.pill.liveOutline{box-shadow:inset 0 0 0 1.5px var(--live);color:#ff8a80}
.pill.final,.pill.notStarted,.pill.noTime{box-shadow:inset 0 0 0 1.5px rgba(255,255,255,.45);color:var(--sub)}
.pill.likelyOver,.pill.delayed,.pill.postponed,.pill.onNow{background:rgba(255,255,255,.16);color:#fff}
.chan{color:var(--sub);overflow:hidden;text-overflow:ellipsis}
.prog{position:absolute;left:0;right:0;bottom:0;height:3px;background:rgba(255,255,255,.12)}
.prog i{display:block;height:3px;background:#fff}
.bases{display:inline-flex;gap:2px;transform:rotate(45deg);margin:0 4px}
.bases i{width:6px;height:6px;border:1px solid var(--sub)}
.bases i.on{background:#ffd54f;border-color:#ffd54f}
.situ{font-size:12px;color:#ffd54f;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
aside{position:fixed;right:0;top:0;bottom:0;width:420px;background:var(--panel);border-left:1px solid var(--line);padding:22px 24px;overflow:auto;transform:translateX(100%);transition:transform .15s;z-index:10}
aside.open{transform:none}
body.carded header,body.carded main{margin-right:420px}
aside h3{margin:4px 0 6px;font-size:22px}
aside .k{font-size:12px;font-weight:700;letter-spacing:1px;color:var(--sub);text-transform:uppercase;margin-top:18px}
aside .v{margin-top:4px}
aside ul{margin:6px 0 0;padding-left:18px}
aside li{margin:3px 0}
aside li small{color:var(--sub)}
aside .close{position:absolute;right:16px;top:14px}
aside code{font-size:12px;color:var(--sub);word-break:break-all}
.empty{color:var(--sub);margin:30px 28px}
</style>
</head>
<body>
<header>
  <h1>Live Events preview</h1>
  <div class="meta" id="meta"></div>
  <div class="bar" id="rows"></div>
  <div class="bar">
    <input type="search" id="q" placeholder="Search teams or titles">
    <label class="opt"><input type="checkbox" id="lean" checked> Hide "Time not listed" and "Event channels"</label>
  </div>
</header>
<main id="board"></main>
<aside id="card"><button class="chip close" onclick="closeCard()">Close</button><div id="cardBody"></div></aside>
<script>
const DATA = __DATA__;
const ICONS = __ICONS__;
const $ = s => document.querySelector(s);
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const PILL = {live:'LIVE', liveOutline:'LIVE', onNow:'ON NOW', delayed:'DELAYED', likelyOver:'LIKELY OVER',
              postponed:'POSTPONED', final:'FINAL', noTime:'NO TIME', notStarted:'NOT STARTED'};
let only = null, picked = null;
$('#meta').textContent = `Made ${DATA.made} from ${DATA.report}` + (DATA.scores ? '' : ' · scores hidden in Options');

function badge(s) {
  if (s.flags && s.flags.length) return `<span class="flags">${s.flags.map(f => `<img src="${esc(f)}" alt="">`).join('')}</span>`;
  if (s.country && !s.abbr) return `<span class="cc">${esc(s.country)}</span>`;
  const inner = s.logo ? `<img src="${esc(s.logo)}" alt="" onerror="this.replaceWith(document.createTextNode('${esc(s.abbr)}'))">` : esc(s.abbr);
  return `<span class="badge${s.plain ? ' plain' : ''}" style="background:${s.logo ? '#fff' : s.color};box-shadow:inset 0 0 0 2px ${s.alt}">${inner}</span>`;
}
function sideLine(s) {
  const score = DATA.scores && s.score ? `<span class="sc">${esc(s.score)}${s.pens ? ` <small>(${esc(s.pens)})</small>` : ''}</span>` : '';
  return `<div class="side${s.trails ? ' trails' : ''}">${badge(s)}${s.rank ? `<span class="rk">${esc(s.rank)}</span>` : ''}<span class="nm">${esc(s.name)}</span>${s.ball ? '<span class="ball"></span>' : ''}${score}</div>`;
}
function tileHtml(t, row) {
  const key = `${row.id}|${t.n}`;
  let body = '';
  if (t.sides.length === 2) {
    body = sideLine(t.sides[0]) + sideLine(t.sides[1]);
  } else {
    const icon = ICONS[t.icon] ? `<span class="ico">${ICONS[t.icon]}</span>` : (t.symbol ? `<span class="sym">${esc(t.symbol)}</span>` : '');
    body = `<div class="ttl">${icon}${esc(t.title)}</div>`;
    if (t.subtitle) body += `<div class="subt">${esc(t.subtitle)}</div>`;
    if (t.leaders.length) body += t.leaders.slice(0, 2).map(l => `<div class="lead"><b>${esc(l.pos)}</b><span>${esc(l.name)}</span><span>${esc(l.score)}${l.thru ? ` · ${esc(l.thru)}` : ''}</span></div>`).join('');
    if (t.parts.length) body += `<div class="parts">${t.parts.map(p => `<div class="${p.current ? 'cur' : ''}" title="${esc(p.label)}"><i style="width:${Math.round(p.fill * 100)}%"></i></div>`).join('')}</div>`;
    if (t.podium.length) body += `<div class="podium">${t.podium.map(p => `<span style="background:${p.color}">${esc(p.code)}</span>`).join('')}</div>`;
  }
  if (t.situation) body += `<div class="situ">${esc(t.situation)}</div>`;
  const pill = t.pill ? `<span class="pill ${t.pill}">${esc(t.pillText || PILL[t.pill] || '')}</span>` : '';
  const bases = t.bases.length === 3 ? `<span class="bases">${[1, 2, 0].map(i => `<i class="${t.bases[i] ? 'on' : ''}"></i>`).join('')}</span>` : '';
  const status = `<span class="st${t.statusBold ? ' bold' : ''}">${esc(t.status)}</span>${t.statusExtra ? `<span>${esc(t.statusExtra)}</span>` : ''}`;
  const where = t.channel || t.pill2 || '';
  const foot = `<div class="foot">${pill}${bases}${status}${where ? `<span class="chan">· ${esc(where)}</span>` : ''}</div>`;
  const under = t.footer ? `<div class="subt" style="font-size:12px">${esc(t.footer)}</div>` : '';
  const tag = t.tag ? `<span class="tag${t.tagAlert ? ' alert' : ''}">${esc(t.tag)}</span>` : `<span>${esc(t.league)}</span>`;
  const prog = t.progress >= 0 ? `<div class="prog"><i style="width:${Math.round(t.progress * 100)}%"></i></div>` : '';
  return `<div class="tile${t.watchable ? '' : ' dim'}${picked === key ? ' sel' : ''}" data-key="${esc(key)}"><div class="top">${t.favorite ? '★ ' : ''}${tag}<span class="num">#${t.n}</span></div>${body}${foot}${under}${prog}</div>`;
}
function matches(t, q) {
  if (!q) return true;
  const hay = [t.title, t.subtitle, t.heroTitle, t.league, t.channel, ...t.sides.map(s => s.name)].join(' ').toLowerCase();
  return hay.includes(q);
}
function render() {
  const q = $('#q').value.trim().toLowerCase();
  const lean = $('#lean').checked;
  const rows = DATA.rows.filter(r => !(lean && (r.id === 'untimed' || r.id === 'families')));
  $('#rows').innerHTML = `<button class="chip${only ? '' : ' on'}" data-row="">All</button>` +
    rows.map(r => `<button class="chip${only === r.id ? ' on' : ''}" data-row="${esc(r.id)}">${esc(r.title)}${r.live ? '<span class="dot"></span>' : ''}</button>`).join('');
  let html = '';
  for (const r of rows) {
    if (only && r.id !== only) continue;
    let strip = '';
    for (const g of r.groups) {
      const tiles = g.tiles.filter(t => matches(t, q));
      if (!tiles.length) continue;
      strip += `<div class="group"><div class="gt">${esc(g.title || ' ')}</div><div class="tiles">${tiles.map(t => tileHtml(t, r)).join('')}</div></div>`;
    }
    if (!strip) continue;
    html += `<section class="row"><h2>${esc(r.title)} <small>${r.live ? r.live + ' live · ' : ''}${r.count}</small></h2><div class="strip">${strip}</div></section>`;
  }
  $('#board').innerHTML = html || '<div class="empty">Nothing matches.</div>';
}
function find(key) {
  const [rowId, n] = key.split('|');
  const row = DATA.rows.find(r => r.id === rowId);
  for (const g of row.groups) for (const t of g.tiles) if (String(t.n) === n) return [row, g, t];
}
function openCard(key) {
  picked = key;
  const [row, g, t] = find(key);
  const ref = `${row.title} › ${g.title || 'tiles'} #${t.n} · ${t.heroTitle || t.title}`;
  let h = `<div class="k">${esc(t.leagueChip || t.league)}</div><h3>${esc(t.heroTitle || t.title)}</h3>`;
  if (t.heroStatus) h += `<div class="v">${esc(t.heroStatus)}</div>`;
  if (t.venue) h += `<div class="v" style="color:var(--sub)">${esc(t.venue)}</div>`;
  if (t.whereLine) h += `<div class="v">${esc(t.whereLine)}</div>`;
  if (t.subtitle && t.sides.length) h += `<div class="v" style="color:var(--sub)">${esc(t.subtitle)}</div>`;
  for (const s of t.where) h += `<div class="k">${esc(s.title)}</div><ul>${s.sources.map(x => `<li>${esc(x.title)}${x.detail ? ` <small>${esc(x.detail)}</small>` : ''}</li>`).join('')}</ul>`;
  if (t.whereNote) h += `<div class="v" style="color:var(--sub)">${esc(t.whereNote)}</div>`;
  h += `<div class="k">How this tile was made</div><ul>${t.notes.map(n => `<li>${esc(n)}</li>`).join('')}</ul>`;
  h += `<div class="k">For feedback</div><div class="v">${esc(ref)}</div><div class="v"><code>${esc(t.id)}</code></div>`;
  h += `<div class="v"><button class="chip" id="copy">Copy reference</button></div>`;
  $('#cardBody').innerHTML = h;
  $('#card').classList.add('open');
  document.body.classList.add('carded');
  $('#copy').onclick = () => navigator.clipboard.writeText(`${ref}\n${t.id}`).then(() => { $('#copy').textContent = 'Copied'; });
  render();
}
function closeCard() { picked = null; $('#card').classList.remove('open'); document.body.classList.remove('carded'); render(); }
document.addEventListener('click', e => {
  const chip = e.target.closest('[data-row]');
  if (chip) { only = chip.dataset.row || null; render(); window.scrollTo(0, 0); return; }
  const tile = e.target.closest('.tile');
  if (tile) openCard(tile.dataset.key);
});
document.addEventListener('keydown', e => { if (e.key === 'Escape') closeCard(); });
$('#q').addEventListener('input', render);
$('#lean').addEventListener('change', render);
render();
</script>
</body>
</html>
)PAGE";

}  // namespace preview
