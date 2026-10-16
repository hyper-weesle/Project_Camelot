import sys,struct,collections,re
sys.path.insert(0,'/home/claude/satbk')
from dol import Dol
A=Dol('/home/claude/satbk/work/mkw_main.dol'); B=Dol('/home/claude/satbk/work/main.dol')
MF={}
for l in open('/home/claude/satbk/ref/mkw/config/RMCP01/symbols.txt'):
    r=re.match(r'(\S+) = \.(text|init):0x([0-9A-F]+); // type:function size:0x([0-9A-F]+)',l)
    if r: MF[int(r.group(3),16)]=int(r.group(4),16)
pairs=[]
for l in open('/home/claude/satbk/wiicompiled/projects/satbk/symbols/final_funcs.txt'):
    a,b,n=l.split(); pairs.append((int(a,16),int(b,16),n))
m={13:collections.defaultdict(collections.Counter),2:collections.defaultdict(collections.Counter)}
def words(D,a,n):
    o=D.off(a)
    if o is None: return None
    return struct.unpack('>%dI'%(n//4),D.d[o:o+n])
def sda(w):
    op=w>>26
    if op in (32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,14):
        ra=(w>>16)&31
        if ra in (13,2):
            d=w&0xffff
            if d&0x8000: d-=0x10000
            return ra,d
    return None
for a,b,n in pairs:
    sz=MF.get(a)
    if not sz: continue
    wa=words(A,a,sz); wb=words(B,b,sz)
    if not wa or not wb: continue
    # require same opcode skeleton
    if any((x>>26)!=(y>>26) for x,y in zip(wa,wb)): continue
    for x,y in zip(wa,wb):
        sx=sda(x); sy=sda(y)
        if sx and sy and sx[0]==sy[0]:
            m[sx[0]][sx[1]][sy[1]]+=1
import json
out={}
for reg in (13,2):
    for k,c in m[reg].items():
        v,cnt=c.most_common(1)[0]
        out['%d:%d'%(reg,k)]=(v,cnt,len(c))
json.dump(out,open('/tmp/claude-0/sdamap.json','w'))
print(len(out))
for q in sys.argv[1:]:
    reg,off=q.split(':'); print(q, out.get('%s:%d'%(reg,-int(off,16))))
