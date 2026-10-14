"""Reproducible authored procedural assets; unit dimensions, Y up, outward normals.
Stable keys models/primitives/{cube,sphere,cylinder,cone,plane}.obj survive reload.
No custom renderer or transient handles. MeshLoader generates tangents from UV0.
"""
from pathlib import Path
from math import sin, cos, pi, sqrt
root = Path(__file__).resolve().parents[1] / 'engine/assets/models/primitives'
root.mkdir(parents=True, exist_ok=True)
def sub(a,b): return tuple(x-y for x,y in zip(a,b))
def cross(a,b): return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def norm(a):
    length=sqrt(dot(a,a));return tuple(x/length for x in a)
class Mesh:
    def __init__(self): self.vertices=[]
    def tri(self,a,b,c):
        n=cross(sub(b[0],a[0]),sub(c[0],a[0]))
        if dot(n,n)<1e-18:return
        if dot(n,tuple(sum(v[1][i] for v in (a,b,c)) for i in range(3)))<0:b,c=c,b
        self.vertices.extend((a,b,c))
    def quad(self,a,b,c,d):self.tri(a,b,c);self.tri(a,c,d)
    def save(self,name):
        lines=['# Editor primitive v1: unit size, Y up; generator tests/GenerateEditorPrimitives.py','o '+name]
        for p,n,uv in self.vertices:
            lines+=['v '+' '.join(f'{x:.9g}' for x in p),'vn '+' '.join(f'{x:.9g}' for x in n),'vt '+' '.join(f'{x:.9g}' for x in uv)]
        for i in range(0,len(self.vertices),3):lines.append('f '+' '.join(f'{j}/{j}/{j}' for j in range(i+1,i+4)))
        (root/(name+'.obj')).write_text('\n'.join(lines)+'\n',encoding='ascii')
cube=Mesh()
for axis in range(3):
    for sign in (-1,1):
        points=[];n=tuple(sign if k==axis else 0 for k in range(3))
        for u,v in ((0,0),(1,0),(1,1),(0,1)):
            p=[0,0,0];p[axis]=sign*.5;p[(axis+1)%3]=u-.5;p[(axis+2)%3]=v-.5;points.append((p,n,(u,v)))
        cube.quad(*points)
cube.save('cube')
plane=Mesh();plane.quad(*[((x-.5,0,z-.5),(0,1,0),(x,z)) for x,z in ((0,0),(1,0),(1,1),(0,1))]);plane.save('plane')
sphere=Mesh();segments,rings=32,16
def spherepoint(j,k):
    theta=j*pi/rings;phi=k*2*pi/segments;n=(sin(theta)*cos(phi),cos(theta),sin(theta)*sin(phi));return tuple(x*.5 for x in n),n,(k/segments,j/rings)
for j in range(rings):
    for k in range(segments):sphere.quad(spherepoint(j,k),spherepoint(j+1,k),spherepoint(j+1,k+1),spherepoint(j,k+1))
sphere.save('sphere')
for name in ('cylinder','cone'):
    mesh=Mesh()
    for k in range(segments):
        a,b=k*2*pi/segments,(k+1)*2*pi/segments
        def side(angle,y,u,r=.5):
            n=norm((cos(angle),.5 if name=='cone' else 0,sin(angle)));return (r*cos(angle),y,r*sin(angle)),n,(u,y+.5)
        lowerA,lowerB=side(a,-.5,k/segments),side(b,-.5,(k+1)/segments)
        if name=='cone':mesh.tri(lowerA,lowerB,side((a+b)/2,.5,(k+.5)/segments,0))
        else:mesh.quad(lowerA,lowerB,side(b,.5,(k+1)/segments),side(a,.5,k/segments))
        for y in (-.5,.5) if name=='cylinder' else (-.5,):
            n=(0,1 if y>0 else -1,0)
            def cap(angle):return (.5*cos(angle),y,.5*sin(angle)),n,(.5+.5*cos(angle),.5+.5*sin(angle))
            mesh.tri(((0,y,0),n,(.5,.5)),cap(a),cap(b))
    mesh.save(name)
