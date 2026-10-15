"""Check reference integrity and diagnostic coverage without launching the editor."""
import base64
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parent
assets = root/'Assets'
manifest = (root/'VisualFidelityLab.plutoproject').read_text().splitlines()
registered = {line.split('\t')[1] for line in manifest if line.startswith('ASSET\t')}
identities = set()
for ref in registered:
    path = assets/ref.removeprefix('project://')
    assert path.is_file(), f'Missing registered asset: {ref}'
    metadata = path.with_name(path.name+'.plutometa').read_text().splitlines()
    identity = next(line.split('\t')[1] for line in metadata if line.startswith('ID\t'))
    assert identity not in identities, f'Duplicate asset identity: {path}'
    identities.add(identity)
    if path.suffix == '.png':
        assert path.read_bytes().startswith(b'\x89PNG\r\n\x1a\n'), path
        continue
    for match in re.finditer(r'project://[^\t\n\r"]+',path.read_text()):
        assert match[0] in registered, f'Unregistered reference: {match[0]}'

scenes = list((assets/'Scenes').glob('*.plutoscene'))
assert len(scenes) == 4
for scene in scenes:
    entities, parents, component, fields, mains, matrix, probes = set(),[],None,{},0,0,0
    for line in scene.read_text().splitlines():
        tokens = line.split('\t')
        if tokens[0] == 'ENTITY':
            entity,parent = map(int,tokens[1:3])
            assert entity not in entities, f'Duplicate entity in {scene}: {entity}'
            entities.add(entity); parents.append(parent)
            matrix += bool(re.match(r'^(BLACK|GRAY|WHITE|RED|GREEN|BLUE|GOLD|COPPER) M=',tokens[4]))
        elif tokens[0] == 'COMPONENT':
            assert int(tokens[1]) in entities
            component,fields = tokens[2],{}
        elif tokens[0] == 'PROPERTY':
            fields[tokens[1]] = tokens[3]
        elif tokens[0] == 'END_COMPONENT':
            if component == 'CameraComponent':
                mains += fields.get('MainCamera') == 'true'
            if component == 'IblCaptureComponent':
                size = int(fields['Resolution'])
                assert 32 <= size <= 2048
                assert len(base64.b64decode(fields['Capture Pixels'],validate=True)) == size*size*6*4*4
                probes += 1
    assert mains == 1 and matrix == 168 and probes == 2, (scene,mains,matrix,probes)
    assert all(parent == 0 or parent in entities for parent in parents)
    print(f'{scene.name}: {len(entities)} entities; 168 PBR samples; 2 stored probes; one main camera')

views = json.loads((root/'views.json').read_text())
assert len(views) == len({view['name'] for view in views}) == 11
print(f'PASS: {len(registered)} registered assets, four lighting variants, eleven saved views')
