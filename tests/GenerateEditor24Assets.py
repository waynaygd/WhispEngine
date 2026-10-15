"""Deterministic small Radiance HDR with an explicitly authored sun (no image inference)."""
from pathlib import Path
import math,json
root=Path(__file__).resolve().parents[1]/'engine/assets/environments'
root.mkdir(parents=True,exist_ok=True)
w,h=128,64
sun=(0.,math.sin(math.radians(20)),math.cos(math.radians(20)))
pixels=bytearray()
for y in range(h):
    theta=(y+.5)/h*math.pi
    for x in range(w):
        phi=((x+.5)/w-.5)*2*math.pi
        direction=(math.cos(phi)*math.sin(theta),math.cos(theta),math.sin(phi)*math.sin(theta))
        alignment=sum(a*b for a,b in zip(direction,sun))
        base=(.08,.15,.28) if direction[1]>0 else (.03,.04,.05)
        color=(10.,9.,7.) if alignment>math.cos(math.radians(4)) else base
        mantissa,exponent=math.frexp(max(color))
        scale=mantissa*256/max(color)
        pixels.extend([*(min(255,int(c*scale)) for c in color),exponent+128])
# Old flat RGBE scanline encoding is supported by stb_image/Radiance and deterministic.
(root/'editor24_known_sun.hdr').write_bytes(f'#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {h} +X {w}\n'.encode()+pixels)
(root/'editor24_known_sun.sun.json').write_text(json.dumps({'version':1,'directionSkyLocal':sun,'elevationDegrees':20,'azimuthDegrees':0,'diameterDegrees':8,'note':'Authored synthetic sun; metadata is explicit, not a brightest-pixel estimate.'},indent=2)+'\n',encoding='utf-8')
print('Generated known-sun HDR',w,h,sun)
