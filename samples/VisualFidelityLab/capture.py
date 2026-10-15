"""Capture a named view through the native Vulkan scene renderer (stdlib only)."""
import argparse
from pathlib import Path
import struct
import subprocess
import zlib

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('renderer', type=Path, help='Path to PlutoGERenderVisualLab executable')
parser.add_argument('--view', default='Overview', help='Saved camera name; see views.json')
parser.add_argument('--variant', choices=['Daylight','Sunset','Night','LocalOnly'], default='Daylight')
parser.add_argument('--output', type=Path, default=root/'Captures')
parser.add_argument('--no-depth-prepass', action='store_true', help='Capture the single-pass geometry reference')
parser.add_argument('--no-post-processing', action='store_true', help='Capture geometry without camera post-processing')
parser.add_argument('--no-shadows', action='store_true', help='Capture without shadow maps')
args = parser.parse_args()
scene = 'VisualFidelityLab' + ('' if args.variant == 'Daylight' else '_'+args.variant)
output = args.output.resolve()/(args.variant+'_'+args.view.replace(' ','_'))
output.parent.mkdir(parents=True,exist_ok=True)
renderer = args.renderer.resolve()
command = [str(renderer), str(root), str(root/'Assets/Scenes'/f'{scene}.plutoscene'),
           str(output.with_suffix('.ppm')), args.view]
for option, enabled in [('--no-depth-prepass', args.no_depth_prepass),
                        ('--no-post-processing', args.no_post_processing),
                        ('--no-shadows', args.no_shadows)]:
    if enabled:
        command.append(option)
subprocess.run(command, cwd=renderer.parent, check=True)
header = output.with_suffix('.ppm').read_bytes().split(b'\n',3)
assert header[0] == b'P6' and header[2] == b'255'
width,height = map(int,header[1].split())
rgb = header[3]
assert len(rgb) == width*height*3
def chunk(kind,data):
    return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
scanlines = b''.join(b'\0'+rgb[y*width*3:(y+1)*width*3] for y in range(height))
image = b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',width,height,8,2,0,0,0))
image += chunk(b'IDAT',zlib.compress(scanlines,6))+chunk(b'IEND',b'')
output.with_suffix('.png').write_bytes(image)
print(output.with_suffix('.png'))
