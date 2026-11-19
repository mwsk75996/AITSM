"""Build docs/stroemforbrug.html: a self-contained before/after page for #99.

Usage: build_report.py [out.html]

The PNG figures from docs/assets/ppk2 are embedded as data URIs, so the page
can be opened or mailed on its own. The numbers come from docs/stroemforbrug.md.
"""
import base64
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
ASSETS = ROOT / "docs" / "assets" / "ppk2"
out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "docs" / "stroemforbrug.html"


def img(name, alt):
    data = base64.b64encode((ASSETS / name).read_bytes()).decode()
    return f'<img src="data:image/png;base64,{data}" alt="{alt}" loading="lazy">'


def ticks(every_min, batch_every_min, minutes=30):
    """Radio wake-ups on a 30-minute strip; batches are marked differently."""
    marks = []
    t = every_min
    while t <= minutes:
        kind = "batch" if abs(t / batch_every_min - round(t / batch_every_min)) < 1e-9 else "ping"
        marks.append(f'<span class="tick {kind}" style="left:{t / minutes * 100:.2f}%"></span>')
        t += every_min
    return "".join(marks)


PAGE = """<!doctype html>
<html lang="da">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Strømforbrug før og efter</title>
<style>
:root {
  --bg: #f6f7f9; --surface: #ffffff; --ink: #1f2933; --muted: #616e7c; --line: #e4e7eb;
  --before: #c2410c; --before-soft: #fde7da; --after: #1d6fd8; --after-soft: #dbe8fb;
  --idle: #9aa5b1; --good: #15803d;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #111418; --surface: #1a1f25; --ink: #e6e9ed; --muted: #9aa5b1; --line: #2b323a;
    --before: #f08a4b; --before-soft: #3a2418; --after: #6ea8ff; --after-soft: #18263b;
    --idle: #5f6b77; --good: #4ade80;
  }
}
:root[data-theme="dark"] {
  --bg: #111418; --surface: #1a1f25; --ink: #e6e9ed; --muted: #9aa5b1; --line: #2b323a;
  --before: #f08a4b; --before-soft: #3a2418; --after: #6ea8ff; --after-soft: #18263b;
  --idle: #5f6b77; --good: #4ade80;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--ink);
  font: 16px/1.55 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; }
main { max-width: 1040px; margin: 0 auto; padding: 40px 16px 64px; }
h1 { font-size: clamp(1.7rem, 4vw, 2.4rem); line-height: 1.15; margin: 0 0 8px; }
h2 { font-size: 1.35rem; margin: 48px 0 6px; }
.lead { color: var(--muted); margin: 0 0 28px; max-width: 70ch; }
.sub { color: var(--muted); margin: 0 0 18px; max-width: 72ch; }
.card { background: var(--surface); border: 1px solid var(--line); border-radius: 12px; padding: 20px; }
.hero { display: grid; grid-template-columns: 1fr auto 1fr; gap: 16px; align-items: stretch; }
.hero .card { text-align: center; }
.label { font-size: .8rem; letter-spacing: .06em; text-transform: uppercase; color: var(--muted); }
.big { font-size: clamp(2.2rem, 7vw, 3.6rem); font-weight: 750; line-height: 1.1; font-variant-numeric: tabular-nums; }
.before .big { color: var(--before); } .after .big { color: var(--after); }
.note { color: var(--muted); font-size: .92rem; }
.factor { display: flex; flex-direction: column; justify-content: center; align-items: center; padding: 0 8px; }
.factor .big { color: var(--good); font-size: clamp(2rem, 6vw, 3rem); }
.kpis { display: grid; grid-template-columns: repeat(4, 1fr); gap: 12px; margin-top: 16px; }
.kpi .vals { display: flex; align-items: baseline; gap: 8px; flex-wrap: wrap; margin-top: 4px;
  font-variant-numeric: tabular-nums; }
.kpi .b { color: var(--before); font-weight: 650; font-size: 1.25rem; }
.kpi .a { color: var(--after); font-weight: 650; font-size: 1.25rem; }
.kpi .arrow { color: var(--muted); }
.bars { display: grid; gap: 14px; }
.bar-row { display: grid; grid-template-columns: 70px 1fr 92px; gap: 12px; align-items: center; }
.bar { display: flex; height: 34px; border-radius: 6px; overflow: hidden; background: var(--line); }
.seg { height: 100%; display: flex; align-items: center; justify-content: center; font-size: .8rem;
  color: #fff; white-space: nowrap; overflow: hidden; border-right: 2px solid var(--surface); }
.seg:last-child { border-right: 0; }
.seg.ping { background: var(--before); } .seg.batch { background: var(--after); }
.seg.idle { background: var(--idle); }
.legend { display: flex; gap: 18px; flex-wrap: wrap; margin-top: 14px; font-size: .9rem; color: var(--muted); }
.legend i { display: inline-block; width: 12px; height: 12px; border-radius: 3px; margin-right: 6px; vertical-align: -1px; }
.num { text-align: right; font-variant-numeric: tabular-nums; font-weight: 650; }
.strip { position: relative; height: 44px; border-radius: 6px; background: var(--bg); border: 1px solid var(--line); }
.tick { position: absolute; top: 6px; bottom: 6px; width: 6px; margin-left: -3px; border-radius: 3px; }
.tick.ping { background: var(--before); } .tick.batch { background: var(--after); }
.strip-axis { display: flex; justify-content: space-between; font-size: .8rem; color: var(--muted); margin-top: 4px; }
.strip-row { display: grid; grid-template-columns: 70px 1fr 92px; gap: 12px; align-items: center; margin-bottom: 10px; }
.why { display: grid; grid-template-columns: repeat(3, 1fr); gap: 12px; }
.why h3 { margin: 0 0 6px; font-size: 1.02rem; }
.why p { margin: 0; color: var(--muted); font-size: .95rem; }
table { width: 100%; border-collapse: collapse; font-variant-numeric: tabular-nums; }
th, td { text-align: left; padding: 10px 8px; border-bottom: 1px solid var(--line); vertical-align: top; }
th { color: var(--muted); font-weight: 600; font-size: .85rem; }
td.b { color: var(--before); font-weight: 600; } td.a { color: var(--after); font-weight: 600; }
.figs { display: grid; grid-template-columns: 1fr 1fr; gap: 16px; }
figure { margin: 0; }
figure img { width: 100%; height: auto; display: block; border-radius: 8px; border: 1px solid var(--line); background: #fff; }
figcaption { font-size: .9rem; color: var(--muted); margin-top: 8px; }
.tag { display: inline-block; font-size: .75rem; font-weight: 650; padding: 2px 8px; border-radius: 99px; margin-right: 6px; }
.tag.b { background: var(--before-soft); color: var(--before); } .tag.a { background: var(--after-soft); color: var(--after); }
.table-wrap { overflow-x: auto; }
footer { margin-top: 48px; color: var(--muted); font-size: .85rem; }
@media (max-width: 760px) {
  .hero { grid-template-columns: 1fr; } .factor { padding: 4px 0; }
  .kpis, .why, .figs { grid-template-columns: 1fr; }
  .bar-row, .strip-row { grid-template-columns: 56px 1fr 72px; gap: 8px; }
  .seg { font-size: 0; }
}
</style>
</head>
<body>
<main>
<p class="label">AITSM · Thingy:91 X · PPK2-måling 2026-11-19</p>
<h1>Strømforbruget er faldet 4,3 gange</h1>
<p class="lead">Samme enhed, samme opstilling og samme profil: en måling hvert 15. sekund og en
batch hvert 5. minut. Den eneste forskel er firmwaren før og efter strømbesparelsen i #23
(PSM og længere MQTT-keepalive).</p>

<section class="hero" aria-label="Hovedresultat">
  <div class="card before">
    <div class="label">Før · firmware 38d6250</div>
    <div class="big">7,40 mA</div>
    <div class="note">ca. <strong>7,6 døgn</strong> på batteriet</div>
  </div>
  <div class="factor"><div class="big">4,3×</div><div class="note">mindre strøm</div></div>
  <div class="card after">
    <div class="label">Efter · Simple</div>
    <div class="big">1,72 mA</div>
    <div class="note">ca. <strong>33 døgn</strong> på batteriet</div>
  </div>
</section>

<section class="kpis" aria-label="Nøgletal">
  <div class="card kpi"><div class="label">Radioen vågner</div>
    <div class="vals"><span class="b">60</span><span class="arrow">→</span><span class="a">12</span><span class="note">gange i timen</span></div></div>
  <div class="card kpi"><div class="label">Tid med radio aktiv</div>
    <div class="vals"><span class="b">16 %</span><span class="arrow">→</span><span class="a">2,6 %</span></div></div>
  <div class="card kpi"><div class="label">MQTT-keepalive</div>
    <div class="vals"><span class="b">60 s</span><span class="arrow">→</span><span class="a">1200 s</span></div></div>
  <div class="card kpi"><div class="label">Hvilestrøm</div>
    <div class="vals"><span class="b">0,70</span><span class="arrow">→</span><span class="a">0,69 mA</span></div><div class="note">næsten uændret</div></div>
</section>

<h2>Hvor går strømmen hen på 5 minutter?</h2>
<p class="sub">Ladning pr. batchperiode. Før brugte enheden mest strøm på fire keepalives, der hver
forbinder radioen til nettet bare for at sende et ping. Efter er de væk.</p>
<div class="card">
  <div class="bars">
    <div class="bar-row"><strong>Før</strong>
      <div class="bar" role="img" aria-label="Før: keepalive ca. 1600 mC, batch ca. 410 mC, hvile og målinger ca. 210 mC">
        <div class="seg ping" style="width:72%">4 keepalives · 1600 mC</div>
        <div class="seg batch" style="width:18.5%">batch</div>
        <div class="seg idle" style="width:9.5%"></div>
      </div><span class="num">2220 mC</span></div>
    <div class="bar-row"><strong>Efter</strong>
      <div class="bar" style="width:23.2%" role="img" aria-label="Efter: batch ca. 300 mC, hvile og målinger ca. 216 mC">
        <div class="seg batch" style="width:58%"></div>
        <div class="seg idle" style="width:42%"></div>
      </div><span class="num">516 mC</span></div>
  </div>
  <div class="legend"><span><i style="background:var(--before)"></i>MQTT-keepalive (ping)</span>
    <span><i style="background:var(--after)"></i>Batch med 20 målinger</span>
    <span><i style="background:var(--idle)"></i>Hvile og målinger hvert 15. sekund</span></div>
</div>

<h2>Hvornår vågner radioen?</h2>
<p class="sub">Hver streg er én gang radioen forbinder til nettet (ca. 15 s og 300 til 400 mC hver gang),
vist over 30 minutter.</p>
<div class="card">
  <div class="strip-row"><strong>Før</strong><div><div class="strip">{ticks_before}</div></div><span class="num">30 gange</span></div>
  <div class="strip-row"><strong>Efter</strong><div><div class="strip">{ticks_after}</div>
    <div class="strip-axis"><span>0 min</span><span>10</span><span>20</span><span>30 min</span></div></div><span class="num">6 gange</span></div>
  <div class="legend"><span><i style="background:var(--before)"></i>Keepalive</span>
    <span><i style="background:var(--after)"></i>Batch</span></div>
</div>

<h2>Hvorfor hjælper det?</h2>
<div class="why">
  <div class="card"><h3>Et ping koster som en batch</h3>
    <p>Et MQTT-PINGREQ er få bytes, men radioen skal stadig forbinde til nettet og bliver
    forbundet i ca. 17 s. Det kostede 400 mC, lige så meget som at sende 20 målinger.</p></div>
  <div class="card"><h3>Keepalive 60 s → 1200 s</h3>
    <p>Forbindelsen holdes nu i live af batchen hvert 5. minut, så der skal ikke sendes ping
    imellem. Radioen vågner kun, når der faktisk er data.</p></div>
  <div class="card"><h3>PSM lader modemmet sove</h3>
    <p>Mellem batches sover modemmet. Hvilestrømmen er næsten ens, fordi den mest kommer fra
    resten af boardet (bl.a. LED og sensorer), ikke fra modemmet.</p></div>
</div>

<h2>Tallene</h2>
<div class="card table-wrap">
<table>
  <thead><tr><th></th><th>Før (38d6250)</th><th>Efter (Simple)</th></tr></thead>
  <tbody>
    <tr><td>Middelstrøm, stabil drift (25 min)</td><td class="b">7,40 mA</td><td class="a">1,72 mA</td></tr>
    <tr><td>Batteritid, 1350 mAh (teoretisk)</td><td class="b">7,6 døgn</td><td class="a">32,7 døgn</td></tr>
    <tr><td>Radioopvågninger pr. time</td><td class="b">60</td><td class="a">12</td></tr>
    <tr><td>Én keepalive</td><td class="b">400 mC</td><td class="a">ingen</td></tr>
    <tr><td>Én batch</td><td class="b">411 mC</td><td class="a">350 mC</td></tr>
    <tr><td>Én måling</td><td>0,2 mC (25 ms)</td><td>0,2 mC (25 ms)</td></tr>
    <tr><td>Hvilestrøm</td><td>0,70 mA</td><td>0,69 mA</td></tr>
    <tr><td>Højeste spids</td><td class="b">365 mA</td><td class="a">162 mA</td></tr>
    <tr><td>Data pr. time (fra #32)</td><td colspan="2">ca. 16 KB i begge, samme batchprofil</td></tr>
  </tbody>
</table>
</div>

<h2>Målingerne fra PPK2</h2>
<p class="sub">Linjen er gennemsnitsstrømmen, det lyse bånd den højeste sample. Det gule felt er den
markering, tallene øverst i hver graf gælder for.</p>
<div class="figs">
  <figure>{img_before_30}<figcaption><span class="tag b">Før</span>30 minutter: radioen vågner hvert minut.</figcaption></figure>
  <figure>{img_after_30}<figcaption><span class="tag a">Efter</span>30 minutter: radioen vågner hvert 5. minut.</figcaption></figure>
  <figure>{img_keepalive}<figcaption><span class="tag b">Før</span>Én keepalive: 17 s med radioen forbundet for et ping.</figcaption></figure>
  <figure>{img_after_batch}<figcaption><span class="tag a">Efter</span>Én batch: netværk, TLS, MQTT og PUBACK.</figcaption></figure>
  <figure>{img_before_idle}<figcaption><span class="tag b">Før</span>Hvile mellem to målinger: 0,70 mA.</figcaption></figure>
  <figure>{img_after_idle}<figcaption><span class="tag a">Efter</span>Hvile mellem to målinger: 0,69 mA.</figcaption></figure>
</div>

<h2>Forbehold</h2>
<ul class="sub">
  <li>Batteritiden er teoretisk: hele 1350 mAh ved stuetemperatur, uden aldring og kulde.</li>
  <li>Én måling på 30 minutter pr. version, NB-IoT, indendørs. Ved minut 24 før tog én keepalive
  usædvanligt lang tid (1,6 C); uden den er forskellen stadig ca. 4 gange.</li>
  <li>LED'en var tændt i begge målinger, som i drift.</li>
</ul>

<footer>PPK2 som ampere meter, 100 000 samples/s, Thingy:91 X på eget batteri. Detaljer, rådata og
scripts: <code>docs/stroemforbrug.md</code> og <code>Simple/scripts/ppk2/</code> (#99).</footer>
</main>
</body>
</html>
"""

html = PAGE
for key, value in {
    "{ticks_before}": ticks(1, 5),
    "{ticks_after}": ticks(5, 5),
    "{img_before_30}": img("foer-ppk2-30-minutter.png", "Strømforløb over 30 minutter før PSM"),
    "{img_after_30}": img("efter-ppk2-30-minutter.png", "Strømforløb over 30 minutter med PSM"),
    "{img_keepalive}": img("foer-ppk2-keepalive.png", "Én MQTT-keepalive før PSM"),
    "{img_after_batch}": img("efter-ppk2-batch.png", "Én batch med PSM"),
    "{img_before_idle}": img("foer-ppk2-hvile.png", "Hvile mellem to målinger før PSM"),
    "{img_after_idle}": img("efter-ppk2-hvile.png", "Hvile mellem to målinger med PSM"),
}.items():
    html = html.replace(key, value)
out.write_text(html, encoding="utf-8")
print(f"{out}: {len(html) // 1024} KB")
