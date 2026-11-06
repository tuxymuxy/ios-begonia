#!/usr/bin/env python3
"""Summarize unique complete touch traces from a decoded, possibly duplicated log."""
import argparse,json,re,statistics
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('log',type=Path)
a=p.parse_args()
rows=set(tuple(map(int,m)) for m in re.findall(r'tl: i (\d+) posted (\d+) read (\d+) sent (\d+)',a.log.read_text(errors='replace')))
valid=[r for r in rows if r[1]<=r[2]<=r[3]]
def summary(values):
    values=sorted(values)
    return {'median_ms':statistics.median(values)/1000,'p90_ms':values[int(.9*(len(values)-1))]/1000,'max_ms':max(values)/1000} if values else None
print(json.dumps({'unique_samples':len(rows),'valid_samples':len(valid),
 'queue':summary([r[2]-r[1] for r in valid]),
 'read_to_sent_including_logging':summary([r[3]-r[2] for r in valid]),
 'post_to_sent':summary([r[3]-r[1] for r in valid]),
 'note':'Retained complete lines only; duplicated console/loader lines removed. Not a whole-run distribution.'},indent=2))
