"""Generate the self-contained visual lab using only Python's standard library.

Authored sample assets live exclusively below this directory. Re-running this
script updates generated assets deterministically; it never edits other projects.
"""
from __future__ import annotations

import base64
import hashlib
import json
import math
from pathlib import Path
import struct
import uuid
import zlib

ROOT = Path(__file__).resolve().parent
ASSETS = ROOT / 'Assets'
FILES: set[Path] = set()
ROUGHNESS = [.04, .1, .25, .4, .6, .8, 1.0]
METALLIC = [0, .5, 1]
COLOURS = {'BLACK': (.005,.005,.005), 'GRAY': (.18,.18,.18),
           'WHITE': (.8,.8,.8), 'RED': (.65,.025,.025),
           'GREEN': (.025,.5,.06), 'BLUE': (.025,.08,.65),
           'GOLD': (1,.766,.336), 'COPPER': (.955,.638,.538)}

# Five-by-seven bitmap lettering keeps regeneration independent of fonts/Pillow.
FONT_ROWS = {
 'A':'01110/10001/10001/11111/10001/10001/10001', 'B':'11110/10001/10001/11110/10001/10001/11110',
 'C':'01111/10000/10000/10000/10000/10000/01111', 'D':'11110/10001/10001/10001/10001/10001/11110',
 'E':'11111/10000/10000/11110/10000/10000/11111', 'F':'11111/10000/10000/11110/10000/10000/10000',
 'G':'01111/10000/10000/10111/10001/10001/01111', 'H':'10001/10001/10001/11111/10001/10001/10001',
 'I':'11111/00100/00100/00100/00100/00100/11111', 'J':'00111/00010/00010/00010/10010/10010/01100',
 'K':'10001/10010/10100/11000/10100/10010/10001', 'L':'10000/10000/10000/10000/10000/10000/11111',
 'M':'10001/11011/10101/10101/10001/10001/10001', 'N':'10001/11001/10101/10011/10001/10001/10001',
 'O':'01110/10001/10001/10001/10001/10001/01110', 'P':'11110/10001/10001/11110/10000/10000/10000',
 'Q':'01110/10001/10001/10001/10101/10010/01101', 'R':'11110/10001/10001/11110/10100/10010/10001',
 'S':'01111/10000/10000/01110/00001/00001/11110', 'T':'11111/00100/00100/00100/00100/00100/00100',
 'U':'10001/10001/10001/10001/10001/10001/01110', 'V':'10001/10001/10001/10001/10001/01010/00100',
 'W':'10001/10001/10001/10101/10101/10101/01010', 'X':'10001/10001/01010/00100/01010/10001/10001',
 'Y':'10001/10001/01010/00100/00100/00100/00100', 'Z':'11111/00001/00010/00100/01000/10000/11111',
 '0':'01110/10001/10011/10101/11001/10001/01110', '1':'00100/01100/00100/00100/00100/00100/01110',
 '2':'01110/10001/00001/00010/00100/01000/11111', '3':'11110/00001/00001/01110/00001/00001/11110',
 '4':'00010/00110/01010/10010/11111/00010/00010', '5':'11111/10000/10000/11110/00001/00001/11110',
 '6':'01110/10000/10000/11110/10001/10001/01110', '7':'11111/00001/00010/00100/01000/01000/01000',
 '8':'01110/10001/10001/01110/10001/10001/01110', '9':'01110/10001/10001/01111/00001/00001/01110',
 '.':'00000/00000/00000/00000/00000/00110/00110', '-':'00000/00000/00000/11111/00000/00000/00000',
 '=':'00000/11111/00000/11111/00000/00000/00000', '/':'00001/00010/00010/00100/01000/01000/10000',
 ' ':'00000/00000/00000/00000/00000/00000/00000'}


def write(relative: str, content: str | bytes) -> Path:
    path = ROOT / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(content, str):
        content = content.encode('utf-8')
    path.write_bytes(content)
    if path.is_relative_to(ASSETS):
        FILES.add(path)
        identity = uuid.uuid5(uuid.NAMESPACE_URL, 'plutoge:visual-lab:' + relative).hex
        path.with_name(path.name + '.plutometa').write_text(
            f'PLUTOASSET\t1\nID\t{identity}\nIMPORTER_VERSION\t1\n', encoding='utf-8')
    return path


def png(name: str, width: int, height: int, sample) -> str:
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    pixels = b''.join(b'\0' + bytes(c for x in range(width) for c in sample(x,y)) for y in range(height))
    encoded = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', width,height,8,6,0,0,0))
    encoded += chunk(b'IDAT', zlib.compress(pixels, 9)) + chunk(b'IEND', b'')
    write(f'Assets/Textures/{name}.png', encoded)
    return f'project://Textures/{name}.png'


def material(name, color=(.5,.5,.5), metallic=0, roughness=.5, **fields):
    values = dict(Color=','.join(map(str, (*color,1))), Metallic=metallic, Roughness=roughness,
                  SurfaceType='Standard', AlphaMode='Opaque', CastsShadow='true', TwoSided='false',
                  ShaderGraph='engine://builtin/shadergraph/default-lit')
    values.update(fields)
    write(f'Assets/Materials/{name}.plutomaterial', ''.join(f'{k}={v}\n' for k,v in values.items()))
    return f'project://Materials/{name}.plutomaterial'


def label_material(text):
    name = 'Label_' + hashlib.sha256(text.encode()).hexdigest()[:12]
    scale, padding = 4, 12
    width, height = len(text)*6*scale+2*padding, 7*scale+2*padding
    def sample(x,y):
        # Built-in quad UVs address the bottom edge first.
        px,py = (x-padding)//scale,(height-1-y-padding)//scale
        lit = False
        if 0 <= py < 7 and 0 <= px < len(text)*6 and px%6 < 5:
            lit = FONT_ROWS[text[px//6]].split('/')[py][px%6] == '1'
        return (190,225,240,255) if lit else (9,15,23,255)
    texture = png(name,width,height,sample)
    return material(name, (1,1,1), AlbedoTexture=texture,
                    ShaderGraph='engine://builtin/shadergraph/default-unlit'), width/height


def vec(value):
    return ','.join(f'{v:.6f}' for v in value)


class Scene:
    def __init__(self):
        self.lines = ['SCENE\t1']
        self.count = 0
        self.cameras = []

    def entity(self, name, position=(0,0,0), scale=(1,1,1), rotation=(0,0,0), parent=0, active=True):
        self.count += 1
        self.lines.append(f'ENTITY\t{self.count}\t{parent}\t{int(active)}\t{name}\t{vec(position)}\t{vec(rotation)}\t{vec(scale)}')
        return self.count

    def component(self, entity, kind, fields, enabled=True):
        self.lines.append(f'COMPONENT\t{entity}\t{kind}\t{int(enabled)}')
        for key, typ, value in fields:
            self.lines.append(f'PROPERTY\t{key}\t{typ}\t{value}\t0')
        self.lines.append('END_COMPONENT')

    def mesh(self, name, position, scale, mat, primitive='sphere', parent=0, rotation=(0,0,0)):
        e = self.entity(name, position, scale, rotation, parent)
        self.component(e, 'MeshComponent', [('Static',4,'true'),('Visible',4,'true'),('SubmeshIndex',1,-1),
            ('MeshAssetReference',2,f'engine://builtin/mesh/{primitive}'),('MaterialSlots.0.MaterialAsset',2,mat)])
        return e

    def label(self, text, position, width, parent=0):
        mat, aspect = label_material(text)
        return self.mesh(text,position,(width*.5,width/aspect*.5,1),mat,'quad',parent)

    def camera(self, name, eye, target, main=False, preset='Reference'):
        dx,dy,dz = (target[i]-eye[i] for i in range(3))
        pitch = math.degrees(math.atan2(dy,math.hypot(dx,dz)))
        yaw = math.degrees(math.atan2(-dx,-dz))
        # Entities compose Rx * Ry * Rz. Extract that order from a level
        # look-at basis; conventional yaw/pitch alone introduces roll here.
        length = math.sqrt(dx*dx+dy*dy+dz*dz)
        fx,fy,fz = dx/length,dy/length,dz/length
        horizontal = math.hypot(fx,fz)
        rotation = (math.degrees(math.atan2(fy,-fz)), math.degrees(math.asin(-fx)),
                    math.degrees(math.atan2(fx*fy/horizontal,-fz/horizontal)))
        e = self.entity('CAMERA - '+name,eye,rotation=rotation)
        self.component(e,'CameraComponent',[('FOV',0,55),('NearPlane',0,.1),('FarPlane',0,250),
            ('MainCamera',4,str(main).lower()),('RenderRuntimeUI',4,'false'),
            ('PostProcessPresetAsset',2,f'project://PostProcessing/{preset}.plutopostprocess')])
        self.cameras.append(dict(name=name,eye=eye,target=target,pitch=pitch,yaw=yaw,entity=e))

    def light(self,name,position,color,intensity,spot=False,rotation=(0,0,0)):
        e = self.entity(name,position,rotation=rotation)
        self.component(e,'LightComponent',[('LightType',6,2 if spot else 0),('Color',3,vec(color)),
            ('Intensity',0,intensity),('CastsShadows',4,'true'),('Static',4,'true'),
            ('Inner Cone Angle (degrees)',0,20),('Outer Cone Angle (degrees)',0,35)])


def reference_capture(tint, size=32):
    """Analytic diagnostic environment, intentionally not a geometry bake."""
    pixels = []
    for face in range(6):
        for y in range(size):
            for x in range(size):
                u,v = (x+.5)/size*2-1, (y+.5)/size*2-1
                color = tuple(c*(.3 if face == 3 else .8) for c in tint)
                if face == 2 and abs(u)<.6 and abs(v)<.4:
                    color = (6,6,6)
                if x in (size//4,3*size//4) or y in (size//4,3*size//4):
                    color = tuple(c*.15 for c in color)
                pixels.extend((*color,1))
    return base64.b64encode(struct.pack('<'+'f'*len(pixels),*pixels)).decode('ascii')


def preset(name, ssr=True, gi=False, bloom=False):
    effects = [('SSR',ssr,[(0,'Intensity',1),(0,'Max Ray Distance',60),(0,'Thickness',.2),
        (0,'Start Offset',.03),(1,'Steps',96),(1,'Binary Search Steps',6),(0,'Edge Fade',.08)]),
        ('SSAO',True,[(0,'Radius',.8),(0,'Bias',.03),(0,'Intensity',1),(0,'Power',1),
            (1,'Samples',32),(1,'Blur Radius',2),(3,'Half Resolution','true'),(3,'AO Only','false')]),
        ('VCTGI',gi,[(0,'Volume Size',100),(1,'Cascade Count',3),(0,'Intensity',1),
            (0,'Secondary Bounce',1),(1,'Cone Count',4),(3,'World Cache','true')]),
        ('Bloom',bloom,[(0,'Intensity',.08),(0,'Threshold',2),(0,'Radius',.7),(1,'Iterations',5)]),
        ('ToneMapping',True,[(0,'Exposure',1),(0,'Gamma',2.2)])]
    text = f'PostProcessPresetVersion 1 {len(effects)}\n'
    for kind,enabled,params in effects:
        text += f'Effect "{kind}" {int(enabled)} {len(params)}\n'
        text += ''.join(f'Parameter {typ} "{key}" "{value}" 0\n' for typ,key,value in params)
    write(f'Assets/PostProcessing/{name}.plutopostprocess',text)


def generate():
    checker = png('Checker',256,256,lambda x,y: ((210,210,210,255) if (x//32+y//32)%2 else (35,45,55,255)))
    cutout = png('Cutout',256,256,lambda x,y: (160,220,110,255 if (x//32+y//32)%2 else 0))
    normals = png('NormalWaves',256,256,lambda x,y: (int(128+45*math.sin(x/256*math.tau*8)),
        int(128+45*math.sin(y/256*math.tau*8)),235,255))
    roughmap = png('RoughnessRamp',256,64,lambda x,y: (x,x,x,255))
    for name,ssr,gi,bloom in [('Reference',True,False,False),('NoSSR',False,False,False),
                              ('GI',True,True,False),('Presentation',True,False,True)]:
        preset(name,ssr,gi,bloom)
    floor = material('Floor',(.16,.18,.21),roughness=.7)
    dark = material('Fixture',(.025,.035,.05),roughness=.65)
    white = material('White',(.7,.7,.7),roughness=.6)
    chrome = material('Chrome',(.95,.95,.95),1,.04)
    s = Scene()
    s.mesh('Lab foundation',(0,-.3,0),(88,.6,74),floor,'cube')
    s.label('PLUTOGE VISUAL FIDELITY LAB',(0,17,-34),36)
    for bank in range(2):
        for col in range(4):
            color_name,color = list(COLOURS.items())[bank*4+col]
            cx,z = (col-1.5)*12, -13-bank*15
            root = s.entity('01 MATERIAL MATRIX - '+color_name)
            s.mesh('Chart backdrop',(cx,3.4+3.5*bank,z-.8),(11.5,6.8+7*bank,.2),dark,'cube',root)
            s.label(color_name+' - ROUGHNESS',(cx,6.4+7*bank,z-.64),9,root)
            for j,r in enumerate(ROUGHNESS):
                x = cx+(j-3)*1.35
                s.label(f'{r:.2f}',(x,.45+7*bank,z+.1),1.05,root)
                for k,m in enumerate(METALLIC):
                    mat = material(f'{color_name}_M{m}_R{r}',color,m,r)
                    s.mesh(f'{color_name} M={m} R={r}',(x,1.5+7*bank+k*1.5,z),(1,1,1),mat,parent=root)
            s.label('M 0 / 0.5 / 1 BOTTOM TO TOP',(cx,5.7+7*bank,z+.1),9,root)
    # Reflection lane: bright/dark off-screen targets, roughness, silhouettes.
    root = s.entity('02 SSR - MOVE CAMERA ACROSS TARGET EDGES')
    for j,r in enumerate([.04,.15,.35,.65,1]):
        mat = material(f'ReflectionLane{j}',(.65,.65,.65),1,r)
        s.mesh(f'SSR floor R={r}',(-12+j*2.2,.03,4),(2.1,.06,12),mat,'cube',root)
    for i,(color,strength) in enumerate([((1,.05,.03),4),((.05,.3,1),4),((0,0,0),0)]):
        mat = material(f'ReflectionTarget{i}',color,Emission=vec(tuple(c*strength for c in color)),
            ShaderGraph='engine://builtin/shadergraph/default-unlit')
        s.mesh('SSR source '+str(i),(-12+i*4,2.5,-1),(2.4,5,.3),mat,'cube',root)
    s.label('SSR - R 0.04 TO 1 LEFT TO RIGHT',(-7.6,6,-1),13,root)
    # Actual glass slabs and spheres, not opacity-only approximations.
    root = s.entity('03 GLASS - IOR ROUGHNESS THICKNESS')
    for a,ior in enumerate([1.1,1.5,2]):
        for b,r in enumerate([.04,.25,.6]):
            for c,thickness in enumerate([.03,.4]):
                mat = material(f'Glass_I{ior}_R{r}_T{thickness}',(.95,.98,1),roughness=r,
                    SurfaceType='Glass',AlphaMode='Blend',TwoSided='true',CastsShadow='false',
                    Transmission=1,Ior=ior,Thickness=thickness,AttenuationColor='0.85,0.95,1',AttenuationDistance=2)
                s.mesh(f'Glass IOR={ior} R={r} T={thickness}',(5+b*2.8+c*1.2,1.3+a*1.5,3),
                       (1,1,1),mat,parent=root)
    s.mesh('Glass contrast backdrop',(8.4,3,1),(10,7,.2),material('GlassChecker',AlbedoTexture=checker),'cube',root)
    s.label('GLASS - IOR 1.1 / 1.5 / 2',(8.4,7,1.3),11,root)
    for i,transmission in enumerate([0,.5,1]):
        mat = material(f'Transmission{i}',(.9,.95,1),roughness=.1,SurfaceType='Glass',AlphaMode='Blend',
            TwoSided='true',CastsShadow='false',Transmission=transmission,Ior=1.5,Thickness=.2)
        s.mesh(f'Transmission={transmission}',(5+i*2.8,1.2,7),(1.5,1.5,1.5),mat,parent=root)
    for i,tint in enumerate([(1,.15,.05),(.1,1,.2),(.1,.25,1)]):
        mat = material(f'TintedGlass{i}',(1,1,1),roughness=.08,SurfaceType='Glass',AlphaMode='Blend',
            TwoSided='true',CastsShadow='false',Transmission=1,Ior=1.5,Thickness=.4,
            AttenuationColor=vec(tint),AttenuationDistance=.5)
        s.mesh('Tinted absorption '+str(i),(5+i*2.8,3.2,7),(1.5,1.5,1.5),mat,parent=root)
    s.label('TRANSMISSION 0 / 0.5 / 1',(8,5,7),10,root)
    # Closed local-light studio and geometric shadow receivers.
    root = s.entity('04 LOCAL LIGHT STUDIO')
    for name,p,scale in [('Floor',(-29,.05,4),(14,.1,14)),('Back',(-29,4,-3),(14,8,.35)),
                         ('Roof',(-29,8,4),(14,.3,14)),('Left',(-36,4,4),(.3,8,14))]:
        s.mesh(name,p,scale,white,'cube',root)
    for j,r in enumerate([.04,.3,.7,1]):
        s.mesh(f'Studio dielectric {r}',(-34+j*3,1.5,3),(2,2,2),material(f'Studio{j}',(.35,.35,.35),0,r),parent=root)
    s.light('Warm point - inverse square',(-33,5,4),(1,.55,.25),50)
    s.light('Cool spot - shadow cone',(-24,6,6),(.25,.5,1),90,True,(-55,20,0))
    s.label('POINT / SPOT - WARM / COOL',(-29,8.8,-2.8),14,root)
    root = s.entity('05 SHADOW CONTACT AND THIN GEOMETRY')
    s.mesh('Shadow receiver',(29,.02,4),(15,.04,15),white,'cube',root)
    for i,h in enumerate([.05,.25,1,2]):
        s.mesh(f'Floating caster gap={h}',(24+i*3,1+h,1),(1.8,2,1.8),dark,'cube',root)
    for i in range(10):
        s.mesh(f'Thin fence {i}',(23+i*1.3,1.5,-1),(.08,3,.08),dark,'cube',root)
    s.label('SHADOWS - CONTACT / GAPS / THIN CASTERS',(29,6,-2),17,root)
    # Analytic coloured captures give deterministic off-screen fallback coverage.
    root = s.entity('06 CONTROLLED IBL - ANALYTIC REFERENCE CAPTURES')
    s.mesh('IBL platform',(-27,.1,23),(18,.2,13),dark,'cube',root)
    for i,tint in enumerate([(.8,.18,.04),(.04,.2,.8)]):
        e = s.entity('IBL '+('WARM' if i==0 else 'COOL')+' - 4M BLEND',(-30+i*6,3,23))
        s.component(e,'IblCaptureComponent',[('Size',3,'14,8,12'),('Intensity',0,1),('Blend Distance',0,4),
            ('Resolution',1,32),('Far Plane',0,50),('Capture Pixels',2,reference_capture(tint)),('Dirty',4,'false')])
    for i in range(9):
        s.mesh('Probe boundary sample '+str(i),(-35+i*2,1.3,23),(1.6,1.6,1.6),chrome,parent=root)
    s.label('IBL WARM TO COOL - ANALYTIC CAPTURES',(-27,5,21),18,root)
    # Texture interpretation, alpha coverage, and normal/roughness response.
    root = s.entity('07 TEXTURE CHANNELS AND COVERAGE')
    configs = [('ALBEDO',dict(AlbedoTexture=checker)),('NORMAL',dict(NormalTexture=normals)),
               ('ROUGHNESS',dict(RoughnessTexture=roughmap)),
               ('MASK',dict(AlbedoTexture=cutout,AlphaMode='Mask',AlphaCutoff=.5,TwoSided='true'))]
    for i,(name,fields) in enumerate(configs):
        mat = material('Texture_'+name,(.7,.7,.7),roughness=.8,**fields)
        s.mesh(name,(-10+i*3,2,23),(2.5,2.5,2.5),mat,parent=root)
        s.label(name,(-10+i*3,4.2,23),2.8,root)
    for i,alpha in enumerate([.15,.5,.85]):
        mat = material('Blend'+str(i),(.15,.55,.8),roughness=.25,AlphaMode='Blend',
                       Color=f'0.15,0.55,0.8,{alpha}',TwoSided='true',CastsShadow='false')
        s.mesh(f'Alpha blend={alpha}',(-10+i*3,1.8,27),(2.2,2.2,.15),mat,'cube',root)
    s.label('TEXTURE / NORMAL / CHANNEL / ALPHA',(-5.5,6,22),15,root)
    root = s.entity('08 EMISSION HDR AND SUBSURFACE')
    for a,strength in enumerate([.1,1,10]):
        for b,color in enumerate([(1,.03,.03),(.03,1,.03),(.03,.03,1),(1,1,1)]):
            mat = material(f'Emission_{a}_{b}',(.05,.05,.05),Emission=vec(tuple(c*strength for c in color)))
            s.mesh(f'Emission {strength}',(8+b*2,1+a*1.4,23),(1.1,1.1,1.1),mat,parent=root)
    for i,strength in enumerate([0,.5,1]):
        mat = material('Subsurface'+str(i),(.6,.15,.07),roughness=.55,Subsurface=strength,
                       SubsurfaceColor='1,0.25,0.08',SubsurfaceRadius=1)
        s.mesh(f'Subsurface {strength}',(19+i*2,1.7,23),(1.5,3,.35),mat,'cube',root)
    s.light('Subsurface backlight',(21,2.5,20),(1,.75,.5),40)
    s.label('EMISSION 0.1 / 1 / 10 - SUBSURFACE',(16,6,22),19,root)
    s.camera('Overview',(39,30,50),(0,2,-5),True)
    s.camera('Material chart front',(0,8,20),(0,3,-13))
    s.camera('Material chart back',(0,17,3),(0,10,-28))
    s.camera('White material closeup',(6,4,-3),(6,3,-13))
    s.camera('SSR edge sweep',(-7,4,14),(-7,1.5,-1))
    s.camera('Glass',(8.4,4,13),(8.4,3,2))
    s.camera('Local lights',(-29,4,15),(-29,2,2))
    s.camera('Shadows',(30,8,16),(29,1,0))
    s.camera('Probe blend',(-27,4,34),(-27,2,23))
    s.camera('Texture detail',(-5.5,4,33),(-5.5,2,23))
    s.camera('Emission and SSS',(17,5,35),(16,2,23))
    base = list(s.lines)
    variants = [('VisualFidelityLab',(-50,-35,0),6,(1,.95,.88),1,True),
                ('VisualFidelityLab_Sunset',(-6,-65,0),6,(1,.75,.5),1,True),
                ('VisualFidelityLab_Night',(25,-35,0),.05,(.4,.55,1),1,True),
                ('VisualFidelityLab_LocalOnly',(-50,-35,0),0,(1,1,1),0,False)]
    for name,rotation,intensity,color,exposure,sky_enabled in variants:
        s.lines = list(base)
        e = s.entity('LIGHTING - '+name,rotation=rotation)
        s.component(e,'LightComponent',[('LightType',6,1),('Color',3,vec(color)),('Intensity',0,intensity),
            ('CastsShadows',4,'true'),('Shadow Method',6,1),('Static',4,'true'),
            ('Shadow Distance (0 = Camera Far)',0,130),('VSM Pool Pages',1,512)])
        e = s.entity('Physical sky')
        s.component(e,'PhysicalSkyComponent',[('Exposure',0,exposure),('Sun Intensity',0,10),
            ('Rayleigh Strength',0,1),('Mie Strength',0,.65),('Mie Anisotropy',0,.8),
            ('Ozone Strength',0,1),('Night Intensity',0,.06),('Moon Intensity',0,.4),
            ('Ground Color',3,'0.025,0.03,0.04')],sky_enabled)
        write(f'Assets/Scenes/{name}.plutoscene','\n'.join(s.lines)+'\n')
    write('views.json',json.dumps(s.cameras,indent=2)+'\n')
    bookmark_lines = ['PLUTOGE_VIEWPORT_BOOKMARKS 1']
    for c in s.cameras:
        # Editor yaw uses the conventional +X basis; entity yaw uses -Z.
        editor_yaw = -90-c['yaw']
        bookmark_lines.append(json.dumps(c['name'])+' '+ ' '.join(map(str,[*c['eye'],editor_yaw,c['pitch'],55,.1,250,12,0,10])))
    write('ViewportBookmarks.bookmarks','\n'.join(bookmark_lines)+'\n')
    write('.plutoge-editor/VisualFidelityLab.plutoproject.bookmarks','\n'.join(bookmark_lines)+'\n')
    first=s.cameras[0]
    project = ['PLUTOPROJECT\t3','NAME\tVisual Fidelity Lab','ASSET_DIR\tAssets',
        'STARTUP_SCENE\tproject://Scenes/VisualFidelityLab.plutoscene','GRAPHICS_API\tVulkan',
        'WINDOW_TITLE\tPlutoGE Visual Fidelity Lab','WINDOW_SIZE\t1600\t900','VSYNC\t0',
        'RUNTIME_UPSCALER\tNone','RUNTIME_RENDER_SCALE\t1',
        'EDITOR_CAMERA_POSITION\t'+'\t'.join(map(str,first['eye'])),
        f'EDITOR_CAMERA_ROTATION\t{-90-first["yaw"]}\t{first["pitch"]}',
        'EDITOR_CAMERA_SPEED\t12','EDITOR_CAMERA_LENS\t55\t0.1\t250',
        'EDITOR_CAMERA_POST_PROCESS_PRESET\tproject://PostProcessing/Reference.plutopostprocess']
    types={'.plutoscene':'Scene','.plutomaterial':'Material','.png':'Texture','.plutopostprocess':'Post Process Preset'}
    for p in sorted(FILES):
        project.append(f'ASSET\tproject://{p.relative_to(ASSETS).as_posix()}\t{p.stat().st_size}\t{types[p.suffix]}')
    write('VisualFidelityLab.plutoproject','\n'.join(project)+'\n')
    print(f'Generated {len(FILES)} registered assets; 168 PBR matrix samples; {len(s.cameras)} saved views.')


if __name__ == '__main__':
    generate()
