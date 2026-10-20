#!/usr/bin/env python3
"""Push the C names back into IDA.

Every function or global in src/ that carries its original address in a
comment ("/* 0x40b601 ... */" above a definition, or after a declaration)
is renamed in the IDA database through the ida-mcp HTTP endpoint.

    python3 tools/sync_ida_names.py [--dry-run]

Addresses that are not a function start (helpers split out of a larger
original function) and code-segment locals are skipped. When two C names
claim one address the CHOOSE table decides. A static s_foo in file.c
becomes file_foo; struct fields (indented) are not names of their own.
"""
import collections, glob, json, os, re, sys, urllib.request

URL = 'http://127.0.0.1:13337/mcp'
SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src')
DATA_START = 0x47a000      # .rdata and .data; .text ends below
CHOOSE = {0x4122d0: 'app_wndproc', 0x4ba6e0: 'g_video_mode'}
STRUCT_PREFIX = {0x4ba6e4: 'g_video_mode_', 0x4ba6e8: 'g_video_mode_', 0x4ba6ec: 'g_video_mode_'}


def call(name, args):
    req = urllib.request.Request(URL, data=json.dumps({
        'jsonrpc': '2.0', 'id': 1, 'method': 'tools/call',
        'params': {'name': name, 'arguments': args}}).encode(),
        headers={'Content-Type': 'application/json',
                 'Accept': 'application/json, text/event-stream'})
    r = json.load(urllib.request.urlopen(req, timeout=120))
    return r['result'].get('structuredContent') or json.loads(r['result']['content'][0]['text'])


def local_name(path, name):
    """A file's static s_foo is named <file>_foo in IDA, so statics of the
    same name in two files stay apart."""
    if name.startswith('s_'):
        return os.path.splitext(os.path.basename(path))[0] + '_' + name[2:]
    return name


def collect():
    pairs = collections.defaultdict(set)
    # Column 0 only: indented lines are struct fields and locals.
    decl = re.compile(r'^(?:extern\s+)?(?:static\s+)?(?:const\s+)?(?:unsigned\s+)?'
                      r'[A-Za-z_]\w*(?:\s+[A-Za-z_]\w*)?[\s\*]+\**([A-Za-z_]\w*)\s*'
                      r'(\(|\[|=|;)[^/]*?/\*\s*(0x[4-6][0-9a-fA-F]{5})\b')
    for f in glob.glob(os.path.join(SRC, '**', '*.[ch]'), recursive=True):
        lines = open(f, encoding='latin1').read().split('\n')
        for i, l in enumerate(lines):
            m = decl.match(l)
            if m and not l.strip().startswith(('return', 'if', 'case')):
                kind = 'func' if m.group(2) == '(' else 'data'
                pairs[(int(m.group(3), 16), kind)].add(local_name(f, m.group(1)))
            m = re.match(r'^/\*\s*(0x4[0-9a-fA-F]{5})\b', l)
            if m:
                for j in range(i + 1, min(i + 12, len(lines))):
                    d = re.match(r'^(?:static\s+)?[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\(', lines[j])
                    if d and not lines[j].startswith((' ', '\t', '/', '*', '#')):
                        pairs[(int(m.group(1), 16), 'func')].add(d.group(1))
                        break
    out = []
    for (a, kind), names in sorted(pairs.items()):
        name = CHOOSE.get(a) or (sorted(names)[0] if len(names) == 1 else None)
        if name is None:
            print('ambiguous', hex(a), sorted(names))
            continue
        out.append((a, kind, STRUCT_PREFIX.get(a, '') + name))
    return out


def main():
    dry = '--dry-run' in sys.argv
    named = {}
    off = 0
    while True:
        r = call('list_globals', {'queries': {'count': 200, 'offset': off}})['result'][0]
        for x in (x for x in r['data'] if 'name' in x):
            named[int(x['addr'], 16)] = x['name']
        off = r.get('next_offset')
        if not off:
            break
    for a, kind, name in collect():
        if kind == 'func':
            fn = call('lookup_funcs', {'queries': hex(a)})['result'][0].get('fn')
            if not fn or int(fn['addr'], 16) != a or fn['name'] == name:
                continue
            print('func', hex(a), fn['name'], '->', name)
            if not dry:
                call('rename', {'batch': {'func': [{'addr': hex(a), 'name': name}]}})
        elif a >= DATA_START:
            olds = [named[a]] if a in named else [p + '%X' % a for p in
                    ('word_', 'dword_', 'byte_', 'unk_', 'off_', 'stru_')]
            if olds[0] == name:
                continue
            print('data', hex(a), olds[0], '->', name)
            if dry:
                continue
            for old in olds:
                if call('rename', {'batch': {'data': [{'old': old, 'new': name}]}})['data'][0]['ok']:
                    break


if __name__ == '__main__':
    main()
