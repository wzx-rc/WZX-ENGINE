"""WZX shader graph -> Maya / Arnold nodes.

WzxShaderGraph.exe saves its graph (format "wzx-graph") and calls sync_file(path) through the commandPort.
Every graph node is mirrored by an existing Maya or Arnold node (aiStandardSurface, ramp, aiNoise...), tagged
with the graph key and the node id, so edits only create, update, rewire or delete what changed.
The graph JSON is also stored in the scene (network node "<name>_wzxGraph"), so it is saved with the .ma/.mb.
"""
import json
import os
import re

import maya.cmds as cmds
import maya.mel as mel

# graph node type -> Maya node type, creation flag, pins -> attributes
NODES = {
    'shader.standardSurface': dict(
        type='aiStandardSurface', cls='shader', outs={'Shader': 'outColor'},
        ins={'Base': 'base', 'Base Color': 'baseColor', 'Diffuse Roughness': 'diffuseRoughness', 'Metalness': 'metalness',
             'Specular': 'specular', 'Specular Color': 'specularColor', 'Specular Roughness': 'specularRoughness',
             'Specular IOR': 'specularIOR', 'Transmission': 'transmission', 'Transmission Color': 'transmissionColor',
             'Subsurface': 'subsurface', 'Subsurface Color': 'subsurfaceColor', 'Coat': 'coat', 'Coat Roughness': 'coatRoughness',
             'Emission': 'emission', 'Emission Color': 'emissionColor', 'Normal': 'normalCamera'},
        link_only={'Normal'}),
    'shader.mix': dict(type='aiMixShader', cls='shader', outs={'Shader': 'outColor'},
                       ins={'Shader 1': 'shader1', 'Shader 2': 'shader2', 'Mix': 'mix'}),
    'shader.flat': dict(type='aiFlat', cls='shader', outs={'Shader': 'outColor'}, ins={'Color': 'color'}),
    'tex.ramp': dict(type='ramp', cls='texture2d', outs={'Color': 'outColor', 'Alpha': 'outAlpha'},
                     ins={'Color A': 'colorEntryList[0].color', 'Position A': 'colorEntryList[0].position',
                          'Color B': 'colorEntryList[1].color', 'Position B': 'colorEntryList[1].position'}),
    'tex.checker': dict(type='checker', cls='texture2d', outs={'Color': 'outColor', 'Alpha': 'outAlpha'},
                        ins={'Color 1': 'color1', 'Color 2': 'color2', 'Contrast': 'contrast'}),
    'tex.noise': dict(type='aiNoise', cls='texture', outs={'Color': 'outColor'},
                      ins={'Color 1': 'color1', 'Color 2': 'color2', 'Octaves': 'octaves', 'Distortion': 'distortion',
                           'Amplitude': 'amplitude'}),
    'tex.image': dict(type='aiImage', cls='texture', outs={'Color': 'outColor'}, ins={'File': 'filename'}),
    'util.colorCorrect': dict(type='aiColorCorrect', cls='utility', outs={'Color': 'outColor'},
                              ins={'Input': 'input', 'Gamma': 'gamma', 'Hue Shift': 'hueShift', 'Saturation': 'saturation',
                                   'Exposure': 'exposure'}),
    'util.blend': dict(type='blendColors', cls='utility', outs={'Color': 'output'},
                       ins={'Color 1': 'color1', 'Color 2': 'color2', 'Blender': 'blender'}),
    'util.multiply': dict(type='multiplyDivide', cls='utility', outs={'Color': 'output'}, ins={'A': 'input1', 'B': 'input2'}),
    'util.bump': dict(type='aiBump2d', cls='utility', outs={'Normal': 'outValue'},
                      ins={'Bump Map': 'bumpMap', 'Bump Height': 'bumpHeight'}),
    'const.color': dict(type='colorConstant', cls='utility', outs={'Color': 'outColor'}, ins={'Color': 'inColor'}),
    'const.float': dict(type='floatConstant', cls='utility', outs={'Value': 'outFloat'}, ins={'Value': 'inFloat'}),
    'out.material': dict(type='shadingEngine', cls='sg', outs={}, ins={'Surface': 'surfaceShader', 'Displacement': 'displacementShader'}),
    'shader.displacement': dict(type='displacementShader', cls='shader', outs={'Displacement': 'displacement'},
                                ins={'Height': 'displacement', 'Scale': 'scale'}),
}

TAG_GRAPH, TAG_ID, TAG_TYPE = 'wzxGraph', 'wzxNode', 'wzxType'


def graph_key(path):
    return os.path.basename(path).split('.')[0]


def _plugins():
    for p in ('mtoa', 'lookdevKit'):
        if not cmds.pluginInfo(p, q=True, loaded=True):
            try:
                cmds.loadPlugin(p, quiet=True)
            except RuntimeError:
                pass


def _clean(s):
    s = re.sub(r'[^A-Za-z0-9_]', '_', s or 'wzx')
    return s if not s[0].isdigit() else '_' + s


def managed(key):
    """{graph node id: maya node} of a graph (id 0 = the network node holding the graph)."""
    out = {}
    for plug in cmds.ls('*.' + TAG_GRAPH) or []:
        node = plug.split('.')[0]
        try:
            if cmds.getAttr(plug) == key:
                out[cmds.getAttr(node + '.' + TAG_ID)] = node
        except (RuntimeError, ValueError):
            pass
    return out


def _tag(node, key, nid, ntype):
    for a, t in ((TAG_GRAPH, 'string'), (TAG_TYPE, 'string')):
        if not cmds.attributeQuery(a, node=node, exists=True):
            cmds.addAttr(node, ln=a, dt=t)
    if not cmds.attributeQuery(TAG_ID, node=node, exists=True):
        cmds.addAttr(node, ln=TAG_ID, at='long')
    cmds.setAttr(node + '.' + TAG_GRAPH, key, type='string')
    cmds.setAttr(node + '.' + TAG_TYPE, ntype, type='string')
    cmds.setAttr(node + '.' + TAG_ID, nid)


def _create(spec, name):
    cls = spec['cls']
    if cls == 'sg':
        return cmds.sets(renderable=True, noSurfaceShader=True, empty=True, name=name)
    if cls == 'shader':
        return cmds.shadingNode(spec['type'], asShader=True, name=name)
    if cls in ('texture', 'texture2d'):
        node = cmds.shadingNode(spec['type'], asTexture=True, name=name)
        if cls == 'texture2d':   # same wiring as Hypershade's "2D texture (normal)"
            p2d = cmds.shadingNode('place2dTexture', asUtility=True, name=name + '_place2d')
            for a, b in (('outUV', 'uvCoord'), ('outUvFilterSize', 'uvFilterSize')):
                cmds.connectAttr(p2d + '.' + a, node + '.' + b, force=True)
        return node
    return cmds.shadingNode(spec['type'], asUtility=True, name=name)


def _place2d(node):
    c = cmds.listConnections(node + '.uvCoord', s=True, d=False, type='place2dTexture') or []
    return c[0] if c else None


def _delete(node):
    if not cmds.objExists(node):
        return
    p = _place2d(node) if cmds.attributeQuery('uvCoord', node=node, exists=True) else None
    cmds.delete(node)
    if p and cmds.objExists(p):
        cmds.delete(p)


def _ptype(plug):
    try:
        return cmds.getAttr(plug, type=True)
    except (RuntimeError, ValueError):
        return None


def _is_compound3(plug):
    return _ptype(plug) in ("float3", "double3")


def _children(plug):
    node = plug.split('.')[0]
    leaf = plug.split('.')[-1]
    kids = cmds.attributeQuery(leaf.split('[')[0], node=node, listChildren=True) or []
    base = plug.rsplit('.', 1)[0]
    return ['%s.%s' % (base, k) for k in kids]


def _set_value(plug, value):
    try:
        t = cmds.getAttr(plug, type=True)
    except (RuntimeError, ValueError):
        return
    if isinstance(value, list):
        cur = cmds.getAttr(plug)
        cur = list(cur[0]) if cur else []
        if t in ('float3', 'double3') and [round(x, 5) for x in cur] != [round(x, 5) for x in value[:3]]:
            cmds.setAttr(plug, value[0], value[1], value[2], type='double3')
    elif isinstance(value, str):
        if t == 'string' and (cmds.getAttr(plug) or '') != value:
            cmds.setAttr(plug, value, type='string')
    elif isinstance(value, bool):
        if cmds.getAttr(plug) != value:
            cmds.setAttr(plug, value)
    else:
        if t in ('float3', 'double3'):
            _set_value(plug, [value] * 3)
        elif abs(cmds.getAttr(plug) - value) > 1e-6:
            cmds.setAttr(plug, value)


def _sources(plug):
    """Incoming connections of a plug and of its children."""
    plugs = [plug] + (_children(plug) if _is_compound3(plug) else [])
    out = []
    for p in plugs:
        for s in cmds.listConnections(p, s=True, d=False, p=True, sh=True) or []:
            out.append((s, p))
    return sorted(out)


def _desired(src, dst):
    """Connections making src drive dst, converting float <-> colour."""
    st, dt = _ptype(src), _ptype(dst)
    s3, d3 = st in ("float3", "double3"), dt in ("float3", "double3")
    if s3 == d3 or st is None or dt is None or "surfaceShader" in dst or "displacementShader" in dst:
        return [(src, dst)]
    if d3:   # value -> colour: the three channels
        return [(src, c) for c in _children(dst)]
    node = src.split('.')[0]   # colour -> value: alpha when the texture has one, else the first channel
    if cmds.attributeQuery('outAlpha', node=node, exists=True):
        return [(node + '.outAlpha', dst)]
    return [(_children(src)[0], dst)]


def _src_plug(src, nodes, have):
    """Maya plug of a graph output (node id, pin name); a channel pin ('Color.R') is a child of the colour plug."""
    if not src or src[0] not in have or src[0] not in nodes:
        return None
    outs = NODES.get(nodes[src[0]]['type'], {}).get('outs', {})
    node = have[src[0]]
    if src[1] in outs:
        return node + '.' + outs[src[1]]
    base, _, ch = src[1].rpartition('.')
    if base in outs and ch and ch in 'RGBXYZ':
        kids = _children(node + '.' + outs[base])
        idx = 'RGB'.find(ch) if ch in 'RGB' else 'XYZ'.find(ch)
        if idx < len(kids):
            return kids[idx]
    return None


def sync(data, key, editor_data=None):
    _plugins()
    name = _clean(data.get('name') or key)
    nodes = {n['id']: n for n in data.get('nodes', [])}
    links = {}
    for l in data.get('links', []):
        links[(l['to'][0], l['to'][1])] = (l['from'][0], l['from'][1])
    undo = cmds.undoInfo(q=True, state=True)
    cmds.undoInfo(stateWithoutFlush=False)
    errors = []
    try:
        have = managed(key)
        # the graph itself, saved with the scene
        holder = have.get(0)
        if not holder or not cmds.objExists(holder):
            holder = cmds.createNode('network', name=name + '_wzxGraph')
            _tag(holder, key, 0, 'graph')
            cmds.addAttr(holder, ln='graphJson', dt='string')
        cmds.setAttr(holder + '.graphJson', json.dumps(editor_data if editor_data is not None else data), type='string')
        # delete what left the graph or changed type
        for nid, node in list(have.items()):
            if nid == 0:
                continue
            n = nodes.get(nid)
            if not n or n['type'] not in NODES or cmds.getAttr(node + '.' + TAG_TYPE) != n['type']:
                _delete(node)
                del have[nid]
        # create / rename
        for nid, n in nodes.items():
            spec = NODES.get(n['type'])
            if not spec:
                continue
            want = name + 'SG' if spec['cls'] == 'sg' else '%s_%s_%d' % (name, _clean(spec['type']), nid)
            node = have.get(nid)
            if not node or not cmds.objExists(node):
                node = _create(spec, want)
                _tag(node, key, nid, n['type'])
                have[nid] = node
            elif node != want and not cmds.objExists(want):
                have[nid] = node = cmds.rename(node, want)
        if holder != name + '_wzxGraph' and not cmds.objExists(name + '_wzxGraph'):
            cmds.rename(holder, name + '_wzxGraph')
        # values and connections
        for nid, n in nodes.items():
            spec = NODES.get(n['type'])
            node = have.get(nid)
            if not spec or not node:
                continue
            try:
                values = n.get('in', {})
                if n['type'] == 'tex.ramp':
                    cmds.setAttr(node + '.type', 1 if values.get('U Direction') else 0)
                    for i in cmds.getAttr(node + '.colorEntryList', multiIndices=True) or []:
                        if i > 1:   # two entries: Color A and Color B
                            cmds.removeMultiInstance('%s.colorEntryList[%d]' % (node, i), b=True)
                if n['type'] == 'util.multiply':
                    cmds.setAttr(node + '.operation', 1)
                if n['type'] == 'tex.noise' and 'Scale' in values:
                    _set_value(node + '.scale', [values['Scale']] * 3)
                if 'Repeat' in values:
                    if n['type'] == 'tex.image':
                        _set_value(node + '.sscale', values['Repeat'])
                        _set_value(node + '.tscale', values['Repeat'])
                    else:
                        p = _place2d(node)
                        if p:
                            _set_value(p + '.repeatU', values['Repeat'])
                            _set_value(p + '.repeatV', values['Repeat'])
                for pin, attr in spec['ins'].items():
                    plug = node + '.' + attr
                    want = []
                    sp = _src_plug(links.get((nid, pin)), nodes, have)
                    if sp:
                        want = _desired(sp, plug)
                    # channel pins (R, G, B / X, Y, Z) connected one by one, as Maya's expanded attributes
                    kids, linked = [], set()
                    if not want and _is_compound3(plug):
                        kids = _children(plug)[:3]
                        letters = 'XYZ' if pin == 'Normal' else 'RGB'
                        for k, kid in enumerate(kids):
                            sp = _src_plug(links.get((nid, pin + '.' + letters[k])), nodes, have)
                            if sp:
                                want += _desired(sp, kid)
                                linked.add(k)
                    have_c = _sources(plug)
                    if sorted(want) != have_c:
                        for s, d in have_c:
                            cmds.disconnectAttr(s, d)
                        for s, d in want:
                            cmds.connectAttr(s, d, force=True)
                    if pin in values and pin not in spec.get('link_only', ()):
                        if not want:
                            _set_value(plug, values[pin])
                        elif linked and isinstance(values[pin], list):   # the channels left free keep the colour's value
                            for k, kid in enumerate(kids):
                                if k not in linked:
                                    _set_value(kid, values[pin][k])
            except Exception as e:   # one bad node must not stop the others
                errors.append('%s: %s' % (node, e))
    finally:
        cmds.undoInfo(stateWithoutFlush=undo)
    if errors:
        cmds.warning('WZX shader graph: ' + '; '.join(errors))
        return 'sync errors: ' + errors[0]
    return 'synced %d nodes' % len(nodes)


def sync_file(path, graph_path=None):
    """path: graph to build (compounds already expanded); graph_path: the editor graph kept in the scene."""
    with open(path, 'r', encoding='utf-8') as f:
        data = json.load(f)
    editor = None
    if graph_path:
        with open(graph_path, 'r', encoding='utf-8') as f:
            editor = json.load(f)
    return sync(data, graph_key(graph_path or path), editor)


def _sg(path):
    for nid, node in managed(graph_key(path)).items():
        if cmds.nodeType(node) == 'shadingEngine':
            return node
    return None


def assign(path):
    sg = _sg(path)
    sel = cmds.ls(sl=True, dag=True, shapes=True, noIntermediate=True) or cmds.ls(sl=True)
    if not sg:
        cmds.warning('WZX shader graph: add a Material Output node first')
        return 'no Material Output'
    if not sel:
        cmds.warning('WZX shader graph: select objects to assign %s' % sg)
        return 'select objects first'
    cmds.sets(sel, e=True, forceElement=sg)
    return 'assigned %s to %d object(s)' % (sg, len(sel))


def show_in_hypershade(path):
    sg = _sg(path)
    nodes = [n for n in managed(graph_key(path)).values() if cmds.nodeType(n) != 'network']
    mel.eval('HypershadeWindow;')
    if nodes:
        cmds.select(sg or nodes, r=True, ne=True)
        try:
            mel.eval('hyperShadePanelGraphCommand("hyperShadePanel1", "showUpAndDownstream");')
        except RuntimeError:
            pass
    return 'hypershade'


def graphs():
    """Graph keys of the scene, with the JSON stored for each."""
    out = {}
    for plug in cmds.ls('*.graphJson') or []:
        node = plug.split('.')[0]
        if cmds.attributeQuery(TAG_GRAPH, node=node, exists=True):
            out[cmds.getAttr(node + '.' + TAG_GRAPH)] = cmds.getAttr(plug)
    return out
