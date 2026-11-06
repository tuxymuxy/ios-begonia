#!/usr/bin/env python3
"""Decode PLL/divider observations; ARMPLL source verified against MT6785 config header."""
import argparse,json,re
from pathlib import Path
# Hardware selector fractions from pinned MT6785 cpufreq register definitions.
DIVIDERS={8:(1,1),9:(3,4),10:(1,2),11:(1,4),16:(1,1),17:(4,5),
          18:(3,5),19:(2,5),20:(1,5),24:(1,1),25:(5,6),26:(2,3),
          27:(1,2),28:(1,3),29:(1,6)}
def decode(con1,divider):
    pcw=con1&0x3fffff
    post=(con1>>24)&7
    selector=(divider>>17)&31
    ratio=DIVIDERS.get(selector)
    pll_hz=pcw*26_000_000/(16384*(1<<min(post,4)))
    return {'pll_con1':hex(con1),'divider':hex(divider),'mux':(divider>>9)&3,
            'divider_selector':selector,'pll_hz':pll_hz,
            'pll_path_hz':pll_hz*ratio[0]/ratio[1] if ratio else None,
            'selected_hz':pll_hz*ratio[0]/ratio[1] if ratio and ((divider>>9)&3)==1 else None,
            'note':'Mux1 is ARMPLL in pinned MT6785 config; other sources are not decoded.'}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);a=p.parse_args()
    matches=re.findall(r'LL pll=([0-9a-f]+) div=([0-9a-f]+) BIG pll=([0-9a-f]+) div=([0-9a-f]+) CCI pll=([0-9a-f]+) div=([0-9a-f]+)',a.log.read_text(errors='replace'))
    if not matches:p.error('no complete CPU clock observation retained')
    v=[int(x,16) for x in matches[0]]
    print(json.dumps({name:decode(v[i*2],v[i*2+1]) for i,name in enumerate(['A55','A76','CCI'])},indent=2))
