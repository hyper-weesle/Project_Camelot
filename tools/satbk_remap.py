import re,os,sys,collections
import os
W=os.environ.get('SATBK_SYMDIR', os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','projects','satbk','symbols'))+'/'
ROOTS=sys.argv[1:]
fm={}
for l in open(W+'final_funcs.txt'):
    p=l.split(); fm[int(p[0],16)]=int(p[1],16)
dm={}
for l in open(W+'xdata.txt'):
    p=l.split(); a,b=int(p[0],16),int(p[1],16); c,t=map(int,p[2].split('/'))
    if c*2>t: dm[a]=b
MD={}
for l in open(W+'mkw_rmcp01_symbols.txt'):
    r=re.match(r'(\S+) = \.(\w+):0x([0-9A-F]+); // type:(\w+)(?: size:0x([0-9A-F]+))?',l)
    if r and r.group(4)!='function': MD[int(r.group(3),16)]=(r.group(1),int(r.group(5),16) if r.group(5) else 0)
def mapaddr(a):
    if a<0x80004000 or a>=0x81000000: return a,'keep'
    if a in fm: return fm[a],'func'
    if a in dm: return dm[a],'data'
    # inside a known MKW data object whose base is mapped
    for base in sorted((x for x in dm if x<=a and a-x<0x2000),reverse=True):
        if base in MD and a-base < max(MD[base][1],4): return dm[base]+(a-base),'member'
        break
    return None,'unmapped'
report=collections.defaultdict(list)
hexlit=re.compile(r'0x(8[0-1][0-9A-Fa-f]{6})(u|U)?\b')
ovr=re.compile(r'(PPC_NATIVE_OVERRIDE(?:_VOID)?\s*\(\s*)([0-9A-Fa-f]{8})(\s*,)')
fatal=re.compile(r'GX_FATAL_STUB\s*\(\s*([0-9A-Fa-f]{8})')
for root,_,fs in (x for R in ROOTS for x in os.walk(R)):
    for fn in fs:
        if not fn.endswith(('.cpp','.h','.inc','.hpp')): continue
        p=os.path.join(root,fn); s=open(p,errors='ignore').read(); orig=s
        lines=s.split('\n'); out=[]
        pending_comment=[False]
        for ln,line in enumerate(lines):
            if pending_comment[0]:
                out.append('//   '+line)
                if line.split('//')[0].rstrip().endswith(';'): pending_comment[0]=False
                continue
            stripped=line.lstrip()
            if stripped.startswith('//') and 'RECOMP_' not in stripped:
                out.append(line); continue
            # whole-line drops for unmapped overrides
            m=ovr.search(line)
            if m:
                a=int(m.group(2),16); b,k=mapaddr(a)
                if b is None or k not in ('func',):
                    report['dropped_override'].append('%s:%d %08x %s'%(fn,ln+1,a,line.strip()[:90]))
                    out.append('// SATBK-UNMAPPED(MKW %08x): '%a+line)
                    code=line.split('//')[0].rstrip()
                    if not code.endswith(';'):
                        pending_comment[0]=True
                    continue
                line=line[:m.start(2)]+'%08X'%b+line[m.end(2):]
            m=fatal.search(line)
            if m:
                a=int(m.group(1),16); b,k=mapaddr(a)
                out.append('// SATBK-FATALSTUB-DROPPED: '+line); continue
            def rep(mo):
                a=int(mo.group(1),16); b,k=mapaddr(a)
                if b is None:
                    report['unmapped_literal'].append('%s:%d %08x | %s'%(fn,ln+1,a,line.strip()[:100])); return mo.group(0)
                if k=='member': report['member'].append('%s:%d %08x->%08x'%(fn,ln+1,a,b))
                return '0x%08X%s'%(b,mo.group(2) or '')
            line=hexlit.sub(rep,line)
            def frep(mo):
                a=int(mo.group(1),16)
                return 'func_%08X'%fm[a] if a in fm else mo.group(0)
            if 'PPC_NATIVE_OVERRIDE' not in line:
                line=re.sub(r'\bfunc_(8[0-9A-Fa-f]{7})\b',frep,line)
            out.append(line)
        s='\n'.join(out)
        if s!=orig: open(p,'w').write(s)
with open(os.environ.get('SATBK_REMAP_REPORT','/tmp/satbk_remap_report.txt'),'w') as f:
    for k,v in report.items():
        f.write('== %s (%d)\n'%(k,len(v))); f.write('\n'.join(v)+'\n')
for k,v in report.items(): print(k,len(v))
