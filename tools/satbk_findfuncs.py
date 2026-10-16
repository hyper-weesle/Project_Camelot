import sys, struct
sys.path.insert(0,'/home/claude/satbk')
from dol import Dol
d=Dol(sys.argv[1])
text=[(a,s,o) for a,s,o,t in d.secs if t]
data=[(a,s,o) for a,s,o,t in d.secs if not t]
def intext(x): return any(a<=x<a+s for a,s,o in text)
def w(a): return d.r32(a)
blr=0x4E800020; bctr=0x4E800420
# 1. jump tables: lis rT,hi ; addi rT,rT,lo ... (lwzx rX, rT, rI | lwzx rX, rI, rT) ; mtctr rX ; bctr
jt_entries=set()
def simm(i): v=i&0xffff; return v-0x10000 if v&0x8000 else v
for a,s,o in text:
    for x in range(a+40,a+s,4):
        if w(x)!=bctr: continue
        # look back for lwzx
        regs={}
        base=None
        for y in range(x-4,max(a,x-80),-4):
            i=w(y)
            if (i>>26)==31 and ((i>>1)&0x3ff)==23:  # lwzx
                ra=(i>>16)&31; rb=(i>>21)&31; rB=(i>>11)&31
                # find lis/addi for ra or rB earlier
                for z in range(y-4,max(a,y-120),-4):
                    j=w(z); op=j>>26
                    if op==14 and ((j>>21)&31) in (ra,rB):
                        rt=(j>>21)&31; lo=simm(j)
                        for q in range(z-4,max(a,z-120),-4):
                            k=w(q)
                            if (k>>26)==15 and ((k>>21)&31)==((j>>16)&31):
                                base=(((k&0xffff)<<16)+lo)&0xffffffff; break
                        break
                break
        if base is None: continue
        # walk table
        p=base; n=0
        while n<512:
            try: v=w(p)
            except Exception: break
            if not intext(v) or abs(v-x)>0x10000: break
            jt_entries.add(p); p+=4; n+=1
starts=set([d.entry])
def is_prologue(i): return (i>>16)==0x9421 or i==0x7C0802A6
def plausible(i):
    op=i>>26
    return i!=0 and op not in (0,1,5,6,22,56,57,58,60,61,62)
for a,s,o in text:
    prev=None
    for x in range(a,a+s,4):
        i=w(x); op=i>>26
        if op==18 and (i&3)==1:
            off=i&0x03FFFFFC
            if off&0x02000000: off-=0x04000000
            t=(x+off)&0xFFFFFFFF
            if intext(t): starts.add(t)
        if prev is not None and is_prologue(i) and (prev==blr or (prev>>26==18 and (prev&3)==0) or prev==0 or prev==0x4C000064):
            starts.add(x)
        prev=i
dp=0
for a,s,o in data:
    for x in range(a,a+s-3,4):
        if x in jt_entries: continue
        v=w(x)
        if v&3==0 and intext(v) and plausible(w(v)):
            pv=w(v-4) if intext(v-4) else 0
            if pv in (blr,0,0x4C000064,bctr) or is_prologue(w(v)) or (pv>>26==18 and (pv&3)==0):
                starts.add(v); dp+=1
print(len(starts),'starts;',dp,'data ptrs;',len(jt_entries),'jump-table entries excluded',file=sys.stderr)
sys.stdout.write(''.join('%08x\n'%s for s in sorted(starts)))
