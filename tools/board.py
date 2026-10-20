"""Load board/<name>.json; values are returned as ints. Warns about unverified ones."""
import json, os, sys

def load(name='miatoll', quiet=False):
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'board', name + '.json')
    raw = json.load(open(p))
    out = {'name': raw['name'], 'soc': raw['soc'], 'ecid_tag': raw['ecid_tag']}
    bad = []
    for k, d in raw.items():
        if not isinstance(d, dict):
            continue
        v = d['v']
        out[k] = int(v, 0) if isinstance(v, str) and v[:1].isdigit() else v
        if not d.get('verified'):
            bad.append(k)
    if bad and not quiet:
        print('board %s: UNVERIFIED (sweet copy): %s' % (name, ', '.join(bad)), file=sys.stderr)
    return out
