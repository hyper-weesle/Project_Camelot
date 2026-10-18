# Generates satbk/runtime_src/satbk_button_glyphs.inc: original button artwork for the PC port (no game art).
from PIL import Image, ImageDraw, ImageFont
import numpy as np
FONT='/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
S=8; N=32
def canvas(n=N): return Image.new('RGBA',(n*S,n*S),(0,0,0,0))
def finish(big,n=N): return big.resize((n,n),Image.LANCZOS)
def disc(d, rim=(20,20,22,255), body_top=(70,72,78), body_bot=(28,29,32)):
    big=canvas(); dd=ImageDraw.Draw(big)
    dd.ellipse((1*S,1*S,(N-1)*S,(N-1)*S),fill=rim)
    # vertical gradient body
    g=np.zeros((N*S,N*S,4),np.uint8)
    for y in range(N*S):
        t=y/(N*S); c=[int(body_top[i]*(1-t)+body_bot[i]*t) for i in range(3)]
        g[y,:,:3]=c; g[y,:,3]=255
    mask=Image.new('L',(N*S,N*S),0); ImageDraw.Draw(mask).ellipse((3*S,3*S,(N-3)*S,(N-3)*S),fill=255)
    big.paste(Image.fromarray(g),(0,0),mask)
    hl=Image.new('L',(N*S,N*S),0); ImageDraw.Draw(hl).ellipse((7*S,4*S,(N-7)*S,15*S),fill=45)
    big.paste(Image.new('RGBA',(N*S,N*S),(255,255,255,255)),(0,0),hl)
    return big
def text_center(big,txt,color,size):
    dd=ImageDraw.Draw(big); f=ImageFont.truetype(FONT,size*S)
    bb=dd.textbbox((0,0),txt,font=f); w,h=bb[2]-bb[0],bb[3]-bb[1]
    W,H=big.size
    dd.text(((W-w)//2-bb[0],(H-h)//2-bb[1]),txt,font=f,fill=color)
def xbox_face(letter,color):
    big=disc(None); text_center(big,letter,color,17); return finish(big)
def ps_face(shape,color):
    big=disc(None); dd=ImageDraw.Draw(big); c=N*S/2; r=7.2*S; w=int(2.6*S)
    if shape=='cross':
        dd.line((c-r,c-r,c+r,c+r),fill=color,width=w); dd.line((c-r,c+r,c+r,c-r),fill=color,width=w)
    elif shape=='circle':
        dd.ellipse((c-r,c-r,c+r,c+r),outline=color,width=w)
    elif shape=='square':
        rr=r*0.85; dd.rectangle((c-rr,c-rr,c+rr,c+rr),outline=color,width=w)
    elif shape=='triangle':
        rr=r*1.05; dd.polygon([(c,c-rr),(c+rr*0.95,c+rr*0.7),(c-rr*0.95,c+rr*0.7)],outline=color,width=w)
    return finish(big)
def pill_small(txt):  # shoulder / trigger / menu buttons
    big=canvas(); dd=ImageDraw.Draw(big)
    dd.rounded_rectangle((1*S,5*S,(N-1)*S,(N-5)*S),radius=7*S,fill=(20,20,22,255))
    dd.rounded_rectangle((3*S,7*S,(N-3)*S,(N-7)*S),radius=5*S,fill=(55,57,62,255))
    text_center(big,txt,(235,235,235,255),11 if len(txt)<=2 else 8)
    return finish(big)
def dpad():
    big=canvas(); dd=ImageDraw.Draw(big)
    dd.rounded_rectangle((1*S,1*S,(N-1)*S,(N-1)*S),radius=6*S,fill=(20,20,22,255))
    dd.rounded_rectangle((3*S,3*S,(N-3)*S,(N-3)*S),radius=4*S,fill=(55,57,62,255))
    c=N*S//2; a=3.6*S; l=10.5*S
    for rect in [(c-a,c-l,c+a,c+l),(c-l,c-a,c+l,c+a)]:
        dd.rounded_rectangle(rect,radius=1.5*S,fill=(215,216,220,255))
    dd.ellipse((c-2*S,c-2*S,c+2*S,c+2*S),fill=(150,150,155,255))
    return finish(big)
def menu_glyph():  # xbox menu (three lines)
    big=disc(None); dd=ImageDraw.Draw(big); c=N*S//2
    for dy in (-5,0,5): dd.rounded_rectangle((c-7*S,c+dy*S-1.2*S,c+7*S,c+dy*S+1.2*S),radius=S,fill=(235,235,235,255))
    return finish(big)
def pill(txt,W,H):
    big=Image.new('RGBA',(W*S,H*S),(0,0,0,0)); d=ImageDraw.Draw(big); r=H*S//2-2*S
    d.rounded_rectangle((1*S,1*S,(W-1)*S,(H-1)*S),radius=r,fill=(10,10,10,255))
    g=np.zeros((H*S,W*S,4),np.uint8)
    for y in range(H*S):
        t=y/(H*S); c=int(250-120*t) if t<0.55 else int(200-60*(t-0.55)/0.45); g[y,:,:3]=c; g[y,:,3]=255
    mask=Image.new('L',(W*S,H*S),0); ImageDraw.Draw(mask).rounded_rectangle((3*S,3*S,(W-3)*S,(H-3)*S),radius=r-2*S,fill=255)
    big.paste(Image.fromarray(g),(0,0),mask)
    hl=Image.new('L',(W*S,H*S),0); ImageDraw.Draw(hl).rounded_rectangle((6*S,4*S,(W-6)*S,(H//2)*S),radius=max(r-4*S,1),fill=70)
    big.paste(Image.new('RGBA',(W*S,H*S),(255,255,255,255)),(0,0),hl)
    f=ImageFont.truetype(FONT,(17 if len(txt)<=5 else 13)*S); bb=d.textbbox((0,0),txt,font=f)
    tw,th=bb[2]-bb[0],bb[3]-bb[1]
    d.text(((W*S-tw)//2-bb[0],(H*S-th)//2-bb[1]),txt,font=f,fill=(55,55,60,255))
    return big.resize((W,H),Image.LANCZOS)

XG,XR,XB,XY=(107,194,48,255),(232,52,52,255),(56,140,232,255),(250,192,30,255)
PB,PR,PP,PG=(130,170,232,255),(240,95,95,255),(226,140,198,255),(70,215,170,255)
glyphs=[
 ('XboxSouth',xbox_face('A',XG)),('XboxEast',xbox_face('B',XR)),('XboxWest',xbox_face('X',XB)),('XboxNorth',xbox_face('Y',XY)),
 ('PsSouth',ps_face('cross',PB)),('PsEast',ps_face('circle',PR)),('PsWest',ps_face('square',PP)),('PsNorth',ps_face('triangle',PG)),
 ('XboxLB',pill_small('LB')),('XboxRB',pill_small('RB')),('XboxLT',pill_small('LT')),('XboxRT',pill_small('RT')),
 ('PsL1',pill_small('L1')),('PsR1',pill_small('R1')),('PsL2',pill_small('L2')),('PsR2',pill_small('R2')),
 ('XboxMenu',menu_glyph()),('PsOptions',pill_small('OPT')),('XboxView',pill_small('VIEW')),('PsCreate',pill_small('SHR')),
 ('XboxLS',pill_small('LS')),('XboxRS',pill_small('RS')),('PsL3',pill_small('L3')),('PsR3',pill_small('R3')),
 ('Dpad',dpad()),
]
pills=[('PillStart',pill('START',96,34)),('PillOptions',pill('OPTIONS',96,34))]
out=['// Generated by satbk/tools/gen_button_glyphs.py - original artwork for the PC port.','#pragma once','#include <cstdint>','namespace satbk_glyphs {']
out.append(f'constexpr uint32_t kGlyphSize = {N};')
def arr(name,im):
    data=im.convert('RGBA').tobytes()
    rows=['    '+', '.join(f'0x{b:02X}' for b in data[i:i+32])+',' for i in range(0,len(data),32)]
    return f'inline const uint8_t k{name}[{len(data)}] = {{\n'+'\n'.join(rows)+'\n};'
for n,im in glyphs: out.append(arr(n,im))
for n,im in pills: out.append(f'constexpr uint32_t k{n}W = {im.width}, k{n}H = {im.height};'); out.append(arr(n,im))
out.append('} // namespace satbk_glyphs')
import os,sys
_a=[a for a in sys.argv[1:] if not a.startswith('--')]
OUT=_a[0] if _a else os.path.join(os.path.dirname(__file__),'..','runtime_src','satbk_button_glyphs.inc')
open(OUT,'w').write('\n'.join(out)+'\n')
# preview sheets (only when asked)
if '--preview' in sys.argv:
  pv=Image.new('RGBA',(len(glyphs)*70,140),(70,70,130,255))
  for i,(n,im) in enumerate(glyphs): pv.alpha_composite(im.resize((64,64),Image.LANCZOS),(i*70+3,3)); pv.alpha_composite(im,(i*70+19,80))
  pv.save('preview.png')
  pp=Image.new('RGBA',(220,50),(70,70,130,255)); pp.alpha_composite(pills[0][1],(5,8)); pp.alpha_composite(pills[1][1],(115,8)); pp.save('preview_pills.png')
