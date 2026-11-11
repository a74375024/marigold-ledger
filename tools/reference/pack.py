"""Packs the lineup reference data (data/lineup-reference.bin) from the
reviewed channel matrix: the viewer's list of paid national and regional
channels and US stations, with an ID column (stable row ids) and our
Aliases column (other names providers use). The matrix itself is never
kept in the repo.

python3 -I tools/reference/pack.py <reviewed matrix csv> data/lineup-reference.bin [made-date]"""
import csv, gzip, io, json, re, sys, unicodedata, collections
import os
REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..') + '/'
SRC = REPO + 'src/core/'
rows = list(csv.DictReader(open(sys.argv[1], encoding='utf-8')))

def fold(s): return unicodedata.normalize('NFKD', s).encode('ascii', 'ignore').decode()
def squash(s): return re.sub(r'[^a-z0-9]', '', fold(s).lower().replace('&', 'and').replace('+', 'plus'))
def slug(s): return re.sub(r'-+', '-', re.sub(r'[^a-z0-9]+', '-', fold(s).lower().replace('&', ' and ').replace('+', ' plus '))).strip('-')

LANG = {'English':'en','Spanish':'es','Arabic':'ar','Hindi':'hi','Mandarin':'zh','Korean':'ko','Portuguese':'pt',
        'Polish':'pl','Tagalog':'tl','Vietnamese':'vi','Punjabi':'pa','Cantonese':'zh','Greek':'el','Bengali':'bn',
        'Malayalam':'ml','Italian':'it','Russian':'ru','Urdu':'ur','Japanese':'ja','Kannada':'kn','French':'fr',
        'Marathi':'mr','German':'de','Tamil':'ta','Telugu':'te','Gujarati':'gu','Armenian':'hy','Hebrew':'he',
        'Farsi':'fa','Persian':'fa','Haitian Creole':'ht','Ukrainian':'uk','Filipino':'tl','Chinese':'zh','Nepali':'ne'}
CAT = {'Local broadcast':'local','International':'international','Entertainment':'entertainment','Spanish-language':'spanish',
       'Sports':'sports','Premium':'movies','News':'news','Business News':'news','Public affairs':'news',
       'Kids & Family':'kids','Documentary & Science':'entertainment','Religious':'entertainment','Lifestyle':'entertainment',
       'Movies':'movies','Music':'music','Shopping':'entertainment','Other':'entertainment','Broadcast network':'entertainment'}
KIND = {'Premium network':'premium','Broadcast network':'broadcast','Regional sports network':'regional',
        'Regional news':'regional','Regional entertainment':'regional'}
NET = {'NBC':'NBC','ABC':'ABC','CBS':'CBS','FOX':'FOX','Fox':'FOX','PBS':'PBS','The CW':'CW','CW':'CW','Telemundo':'TELEMUNDO',
       'Univision':'UNIVISION','UniMás':'UNIMAS','UniMas':'UNIMAS','MyNetworkTV':'MYTV','MyNetwork':'MYTV','ION':'ION',
       'Independent':'INDEPENDENT'}
def lang(s):
    first = re.split(r'[/,]', s)[0].strip()
    return LANG.get(first, '') if first else ''
def net(s): return NET.get(s, s)

# Markets as the app names them.
msrc = open(SRC + 'catalog/markets.cpp', encoding='utf-8').read()
body = msrc[msrc.find('builtinMarkets'):]
app_markets = re.findall(r'"([^"]+)"', body[:body.find('};')])
def mkey(s): return squash(s.replace('&', ' '))
app_by_key = {mkey(a): a for a in app_markets}
def first_city(s): return squash(re.split(r'[–\-,/(]', s.replace('Ft.', 'Fort').replace('St.', 'Saint'))[0])
app_by_first = collections.defaultdict(list)
for a in app_markets: app_by_first[first_city(a)].append(a)
MARKET_FIX = {'Champaign–Urbana–Springfield–Decatur': 'Champaign & Springfield-Decatur', 'Columbus, GA': 'Columbus, GA (Opelika, AL)', 'Columbus–Tupelo–West Point–Starkville': 'Columbus-Tupelo-West Point-Houston'}
def app_market(m):
    if m in MARKET_FIX: return MARKET_FIX[m]
    if mkey(m) in app_by_key: return app_by_key[mkey(m)]
    hits = app_by_first.get(first_city(m), [])
    return hits[0] if len(hits) == 1 else ''

# Logos, resolved here so the app only looks one up.
logo_names = set(re.findall(r'"([a-z0-9-]+)"', open(SRC + 'logos/logo_set.cpp').read().split('kNames = {')[1].split('};')[0]))
flat = {}
for n in logo_names: flat.setdefault(n.replace('-', ''), n)
def logo_for(names):
    for c in names:
        q = squash(c)
        for v in (q, q.removesuffix('tv'), q + 'tv', q.removesuffix('network'), q + 'network', q.removesuffix('channel'), q + 'channel'):
            if v and v in flat: return flat[v] + '-us.png'
    return ''

EXTRA = {'TV One': ['ONE TV']}  # spellings the alias pass folds into the name
def aliases(r):
    out = [a.strip() for a in r['Aliases'].split(';') if a.strip()] + (EXTRA.get(r['Channel'], []) if not r['Feed'] else [])
    out = [a for a in out if not re.search(r'\b(Jan|Feb|Mar|Apr|Jun|Jul|Aug|Sept?|Oct|Nov|Dec)\s+\d', a)]
    # Unaccented spellings too ("Galavision"): providers rarely write accents.
    more = [fold(a) for a in [r['Channel']] + out if fold(a) != a and fold(a).strip()]
    return list(dict.fromkeys(out + more))

# Stations first: each market's states, for regional channels.
STATES = {'AL','AK','AZ','AR','CA','CO','CT','DE','DC','FL','GA','HI','ID','IL','IN','IA','KS','KY','LA','ME','MD','MA','MI','MN',
          'MS','MO','MT','NE','NV','NH','NJ','NM','NY','NC','ND','OH','OK','OR','PA','RI','SC','SD','TN','TX','UT','VT','VA','WA',
          'WV','WI','WY','PR'}
STATE_NAMES = {'alabama':'AL','alaska':'AK','arizona':'AZ','arkansas':'AR','california':'CA','colorado':'CO','connecticut':'CT',
    'delaware':'DE','florida':'FL','georgia':'GA','hawaii':'HI','idaho':'ID','illinois':'IL','indiana':'IN','iowa':'IA','kansas':'KS',
    'kentucky':'KY','louisiana':'LA','maine':'ME','maryland':'MD','massachusetts':'MA','michigan':'MI','minnesota':'MN',
    'mississippi':'MS','missouri':'MO','montana':'MT','nebraska':'NE','nevada':'NV','new hampshire':'NH','new jersey':'NJ',
    'new mexico':'NM','north carolina':'NC','north dakota':'ND','ohio':'OH','oklahoma':'OK','oregon':'OR','pennsylvania':'PA',
    'rhode island':'RI','south carolina':'SC','south dakota':'SD','tennessee':'TN','texas':'TX','utah':'UT','vermont':'VT',
    'virginia':'VA','washington state':'WA','west virginia':'WV','wisconsin':'WI','wyoming':'WY','puerto rico':'PR','hawai':'HI'}
AREA_WORDS = {'bay area': ['San Francisco-Oakland-San Jose'], 'southern california': ['Los Angeles', 'San Diego'],
              'socal': ['Los Angeles', 'San Diego'], 'northern california': ['San Francisco-Oakland-San Jose', 'Sacramento-Stockton-Modesto'],
              'new england': ['Boston (Manchester)'], 'tri-state': ['New York'], 'dc': ['Washington, DC (Hagerstown)'],
              'twin cities': ['Minneapolis-St. Paul'], 'dfw': ['Dallas-Ft. Worth'], 'gulf south': ['New Orleans'],
              'hurricanes': ['Raleigh-Durham (Fayetteville)'], 'upper midwest': ['Minneapolis-St. Paul'], 'new jersey': ['New York', 'Philadelphia']}
market_states = collections.defaultdict(set)
market_count = collections.defaultdict(collections.Counter)
stations = []
for r in rows:
    if r['Scope'] != 'Local': continue
    mk = app_market(r['Market / region'])
    if mk and r['State']: market_states[mk].add(r['State']); market_count[mk][r['State']] += 1
    st = {'c': r['Channel'].upper(), 'n': net(r['Network affiliation'])}
    if mk: st['m'] = mk
    if r['Station class'] in ('Class A', 'Low power'): st['k'] = 'A' if r['Station class'] == 'Class A' else 'LP'
    if r['Virtual channel']: st['v'] = r['Virtual channel']
    l = lang(r['Language'])
    if l and l != 'en': st['l'] = l
    subs = []
    for m in re.finditer(r'\b(NBC|ABC|CBS|FOX|Fox|The CW|CW|MyNetworkTV|MyNetwork|Telemundo|Univision|UniM[aá]s|ION|PBS)\b[^;.]{0,20}?\bon (\d+\.\d+)', r['Notes']):
        n = net(m.group(1))
        if n != st['n'] and not any(s['n'] == n for s in subs): subs.append({'n': n, 'v': m.group(2)})
    if subs: st['s'] = subs
    former = list(dict.fromkeys(re.findall(r'[Ff]ormerly ([KW][A-Z]{2,3})(?:-[A-Z]{2})?\b', r['Notes'])))
    if former: st['f'] = former
    m = re.search(r'sister license of ([KW][A-Z]{2,3}(?:-[A-Z]{2})?)', r['Notes'])
    if m: st['w'] = m.group(1)
    stations.append(st)

home_state = {m: c.most_common(1)[0][0] for m, c in market_count.items()}
def region_markets(text):
    out = []
    t = fold(text)
    low = t.lower()
    for word, mks in AREA_WORDS.items():
        if re.search(r'\b' + re.escape(word) + r'\b', low): out += mks
    states = set(re.findall(r'\b([A-Z]{2})\b', t)) & STATES
    for name, code in STATE_NAMES.items():
        if re.search(r'\b' + name + r'\b', low): states.add(code)
    by_city = collections.defaultdict(list)
    for a in app_markets:
        city = re.split(r'[-,(&]', a)[0].strip().lower().replace('ft.', 'fort')
        if len(city) > 3 and re.search(r'\b' + re.escape(city) + r'\b', low.replace('ft.', 'fort')): by_city[city].append(a)
    for city, mks in by_city.items():
        # "Columbus": the one in a state the text names, else the biggest.
        named = [a for a in mks if home_state.get(a) in states]
        out += named or mks[:1]
    if states and not out:
        out += [a for a in app_markets if home_state.get(a) in states]
    return [m for m in dict.fromkeys(out) if m in app_markets]

# Channels: one per name, its feed rows under it.
# By the matrix's ID column (stable): "<id>", its feeds "<id>~west" etc.
groups = collections.OrderedDict()
for r in rows:
    if r['Scope'] == 'Local': continue
    if r['Marigold review'].startswith('Same channel as'): continue
    groups.setdefault(r['ID'].split('~')[0], []).append(r)
id_of_name = {}
for gid, rs in groups.items():
    b = next((r for r in rs if not r['Feed']), rs[0])
    id_of_name.setdefault(squash(b['Channel']), gid)
    id_of_name.setdefault(squash(b['Channel'].split(' / ')[0]), gid)
channels, taken, unplaced = [], set(), []
for cid, rs in groups.items():
    base = next((r for r in rs if not r['Feed']), rs[0])
    key = squash(base['Channel'])
    assert cid not in taken, cid
    taken.add(cid)
    al = aliases(base)
    # Shown short: "MSG Network / MSG Sportsnet" as MSG Network, "Turner
    # Classic Movies (TCM)" without its bracket; the full names stay other names.
    shown = re.sub(r'\s*\([^)]*\)\s*$', '', base['Channel'].split(' / ')[0]).strip() or base['Channel']
    if shown != base['Channel']:
        al = list(dict.fromkeys([base['Channel']] + [p.strip() for p in base['Channel'].split(' / ')[1:]] + al))
    ch = {'i': cid, 'n': shown}
    if al: ch['a'] = al
    ch['g'] = CAT.get(base['Genre'], 'entertainment')
    k = KIND.get(base['Type'], '')
    if base['Type'] == 'Sub-channel / feed' and base['Genre'] == 'Premium': k = 'premium'
    if base['Scope'] == 'Regional': k = 'regional'
    if base['Scope'].startswith('National (via local') and base['Type'] != 'Broadcast network': k = ''
    if k: ch['k'] = k
    l = lang(base['Language'])
    if l and l != 'en': ch['l'] = l
    if base['Country'] not in ('USA', ''): ch['o'] = base['Country']
    if base['Scope'] == 'Regional':
        mks = region_markets(base['Market / region'])
        if mks: ch['m'] = mks
        else: unplaced.append((base['Channel'], base['Market / region']))
    if base['Parent network'] and squash(base['Parent network']) != key:
        ch['p'] = id_of_name.get(squash(base['Parent network']), slug(base['Parent network']))
    if re.match(r'(this )?(channel )?(was )?shut down', base['Notes'], re.I): ch['x'] = 1
    lg = logo_for([base['Channel']] + al)
    if lg: ch['L'] = lg
    names = {squash(n) for n in [base['Channel']] + al}
    feeds = {}
    for f in rs:
        if not f['Feed']: continue
        own = [a for a in aliases(f) if squash(a) not in names]
        feeds[f['Feed'].lower()] = own
    if feeds: ch['f'] = feeds
    channels.append(ch)

have = {squash(c['n']) for c in channels} | {squash(a) for c in channels for a in c.get('a', [])}
gone = {}
for r in rows:
    for sent in re.split(r'(?<=[.;])\s+', r['Notes']):
        m = re.search(r'([A-Z][^.;]*?)\s+(?:was|were)\s+shut down', sent)
        if not m: continue
        for n in re.split(r',\s*|\s+and\s+', re.sub(r'\s*\(East/West\)', '', m.group(1))):
            n = n.strip()
            if 2 <= len(n) <= 30 and n[0].isupper() and squash(n) not in have and not re.search(r'Network|Sports|FanDuel|Channel$', n):
                gone[squash(n)] = n
                # Providers write the parent in front ("Cinemax ThrillerMAX").
                parent = r['Channel'].split(' / ')[0]
                if not n.lower().startswith(parent.lower()) and squash(parent + n) not in have:
                    gone[squash(parent + n)] = parent + ' ' + n

made = sys.argv[3] if len(sys.argv) > 3 else ''
data = {'made': made, 'channels': channels, 'gone': sorted(gone.values()), 'stations': stations}
raw = json.dumps(data, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
buf = io.BytesIO()
with gzip.GzipFile(fileobj=buf, mode='wb', mtime=0, compresslevel=9) as g: g.write(raw)
open(sys.argv[2], 'wb').write(buf.getvalue())
print('channels', len(channels), 'stations', len(stations), 'gone', sorted(gone.values()), 'bytes', len(raw), '->', len(buf.getvalue()))
print('stations without app market', sum(1 for s in stations if 'm' not in s), sorted({r['Market / region'] for r in rows if r['Scope']=='Local' and not app_market(r['Market / region'])}))
print('regional without markets', len(unplaced), unplaced[:30])
reg = [c for c in channels if 'm' in c]
for c in reg[:12]: print(' ', c['n'], c['m'][:6], len(c['m']))
