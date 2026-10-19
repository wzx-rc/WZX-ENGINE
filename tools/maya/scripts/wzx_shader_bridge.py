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

# The equivalences (graph node -> Maya node, pins -> attributes, value transforms) live in mappings/equivalences.json,
# edited in the Equivalences dashboard of WzxShaderGraph.exe. NODES below is only the fallback when the file is missing.
MAPPING_FILE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'mappings', 'equivalences.json'))
_mapping_cache = {'mtime': None, 'specs': None, 'doc': None}

# graph node type -> Maya node type, creation flag, pins -> attributes (fallback)
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


def _entry(e):
    """A pin rule as a dict: attr (or attrs), link_only, vector, value_map, scale, offset, invert01, clamp, power."""
    return {'attr': e} if isinstance(e, str) else dict(e)


def specs():
    """Maya rules of every graph node type, read from the equivalences file (reloaded when it changes)."""
    try:
        mt = os.path.getmtime(MAPPING_FILE)
    except OSError:
        mt = None
    if mt is not None and mt != _mapping_cache['mtime']:
        try:
            with open(MAPPING_FILE, 'r', encoding='utf-8') as f:
                doc = json.load(f)
            out = {}
            for t, targets in doc.get('nodes', {}).items():
                m = targets.get('maya')
                if not m or not m.get('type'):
                    continue
                out[t] = dict(type=m['type'], cls=m.get('class', 'utility'), ins={p: _entry(e) for p, e in m.get('ins', {}).items()},
                              outs=dict(m.get('outs', {})), fixed=dict(m.get('fixed', {})))
            _mapping_cache.update(mtime=mt, specs=out, doc=doc)
        except (OSError, ValueError) as e:
            cmds.warning('WZX shader graph: equivalences file unreadable (%s), built-in table used' % e)
    if _mapping_cache['specs'] is None:
        _mapping_cache['specs'] = {t: dict(type=s['type'], cls=s['cls'], outs=dict(s['outs']), fixed={},
                                           ins={p: dict(attr=a, link_only=p in s.get('link_only', ())) for p, a in s['ins'].items()})
                                   for t, s in NODES.items()}
    return _mapping_cache['specs']


def _transform(v, e, inverse=False):
    """Value rules of a pin (ChatIt style): scale, offset, invert01, clamp, power, value_map."""
    if isinstance(v, (list, tuple)):
        return [_transform(x, e, inverse) for x in v]
    vm = e.get('value_map')
    if vm:
        if inverse:
            for k, mv in vm.items():
                if mv == v:
                    return k == 'true' if k in ('true', 'false') else k
            return v
        k = ('true' if v else 'false') if isinstance(v, bool) else str(v)
        return vm.get(k, v)
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        return v
    sc, of = float(e.get('scale', 1.0)), float(e.get('offset', 0.0))
    if inverse:
        return (v - of) / sc if sc else v
    v = v * sc + of
    if e.get('invert01'):
        v = 1.0 - v
    if 'clamp' in e:
        v = min(max(v, e['clamp'][0]), e['clamp'][1])
    if 'power' in e:
        v = abs(v) ** float(e['power'])
    return v


def _resolve(node, attr):
    """'place2d.repeatU' is an attribute of the place2dTexture driving the texture."""
    if attr.startswith('place2d.'):
        p = _place2d(node)
        return p + '.' + attr[8:] if p else None
    return node + '.' + attr


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
    outs = specs().get(nodes[src[0]]['type'], {}).get('outs', {})
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
    # two graphs with the same material name: the second one is named after its key
    sg = name + 'SG'
    if cmds.objExists(sg) and cmds.attributeQuery(TAG_GRAPH, node=sg, exists=True) and cmds.getAttr(sg + '.' + TAG_GRAPH) != key:
        name = _clean(key)
    nodes = {n['id']: n for n in data.get('nodes', [])}
    links = {}
    for l in data.get('links', []):
        links[(l['to'][0], l['to'][1])] = (l['from'][0], l['from'][1])
    rules = specs()
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
            if not n or n['type'] not in rules or cmds.getAttr(node + '.' + TAG_TYPE) != n['type']:
                _delete(node)
                del have[nid]
        # create / rename
        for nid, n in nodes.items():
            spec = rules.get(n['type'])
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
            spec = rules.get(n['type'])
            node = have.get(nid)
            if not spec or not node:
                continue
            try:
                values = n.get('in', {})
                if spec['type'] == 'ramp':   # two entries: Color A and Color B
                    for i in cmds.getAttr(node + '.colorEntryList', multiIndices=True) or []:
                        if i > 1:
                            cmds.removeMultiInstance('%s.colorEntryList[%d]' % (node, i), b=True)
                for a, v in spec.get('fixed', {}).items():   # constant attributes of the rule (e.g. multiplyDivide operation)
                    _set_value(node + '.' + a, v)
                for pin, e in spec['ins'].items():
                    if 'attrs' in e:   # value copied to several attributes (repeat U and V...), never connected
                        if pin in values:
                            for a in e['attrs']:
                                p = _resolve(node, a)
                                if p:
                                    _set_value(p, _transform(values[pin], e))
                        continue
                    if not e.get('attr'):
                        continue
                    plug = _resolve(node, e['attr'])
                    if not plug:
                        continue
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
                    if pin in values and not e.get('link_only'):
                        v = _transform(values[pin], e)
                        if e.get('vector') and not isinstance(v, list):
                            v = [v] * 3
                        if not want:
                            _set_value(plug, v)
                        elif linked and isinstance(v, list):   # the channels left free keep the colour's value
                            for k, kid in enumerate(kids):
                                if k not in linked:
                                    _set_value(kid, v[k])
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


# ------------------------------------------------------------------------------------------------ equivalences check

def check_mappings(out_path, mapping_path=None):
    """Creates each Maya node of the rules once and checks that every mapped attribute exists. Writes a JSON report
    {graph type: {type_ok, ins: {pin: ok}, outs: {pin: ok}}} for the Equivalences dashboard."""
    global MAPPING_FILE
    if mapping_path:
        MAPPING_FILE = mapping_path
    _mapping_cache['mtime'] = None
    _plugins()
    report = {}
    undo = cmds.undoInfo(q=True, state=True)
    cmds.undoInfo(stateWithoutFlush=False)
    made = []
    try:
        for t, s in specs().items():
            r = {'type_ok': False, 'ins': {}, 'outs': {}}
            report[t] = r
            try:
                node = _create(s, 'wzxCheck_' + _clean(s['type']))
            except Exception:
                continue
            made.append(node)
            r['type_ok'] = True

            def ok(attr, node=node):
                p = _resolve(node, attr)
                return bool(p) and cmds.objExists(p)
            for pin, e in s['ins'].items():
                attrs = e.get('attrs') or ([e['attr']] if e.get('attr') else [])
                r['ins'][pin] = bool(attrs) and all(ok(a) for a in attrs)
            for pin, a in s['outs'].items():
                r['outs'][pin] = ok(a)
    finally:
        for n in made:
            _delete(n)
        cmds.undoInfo(stateWithoutFlush=undo)
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(report, f)
    bad = sum(1 for r in report.values() for d in (r['ins'], r['outs']) for v in d.values() if not v)
    return 'checked %d nodes, %d attribute(s) not found' % (len(report), bad)


# ------------------------------------------------------------------------------------------------ scene library

def scene_materials(out_path):
    """Materials of the scene for the Scene library: shading group, surface shader, type, objects, WZX graph (if any)."""
    graphs_by_key = graphs()
    out = []
    for sg in cmds.ls(type='shadingEngine') or []:
        if sg == 'initialParticleSE':
            continue
        shader = (cmds.listConnections(sg + '.surfaceShader', s=True, d=False) or [None])[0]
        key = cmds.getAttr(sg + '.' + TAG_GRAPH) if cmds.attributeQuery(TAG_GRAPH, node=sg, exists=True) else None
        members = cmds.sets(sg, q=True) or []
        out.append({'sg': sg, 'shader': shader, 'shaderType': cmds.nodeType(shader) if shader else '',
                    'objects': len(members), 'wzx': key, 'graph': graphs_by_key.get(key) if key else None})
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(out, f)
    return 'scene: %d material(s)' % len(out)


def select_members(sg):
    cmds.select(cmds.sets(sg, q=True) or [], r=True)
    return 'selected objects of %s' % sg


def assign_sg(sg):
    sel = cmds.ls(sl=True, dag=True, shapes=True, noIntermediate=True) or cmds.ls(sl=True)
    if not sel:
        return 'select objects first'
    cmds.sets(sel, e=True, forceElement=sg)
    return 'assigned %s to %d object(s)' % (sg, len(sel))


def import_material(sg, out_path=None):
    """Maya material -> WZX graph, with the rules read backwards. The Maya nodes without a rule are listed (never
    converted silently). The new graph builds its own copy of the network; the original material is left as it is."""
    sp = specs()
    rev = {}
    for t, s in sp.items():
        rev.setdefault(s['type'], (t, s))
    ids, nodes, links, unknown, depth = {}, [], [], [], {}

    def letters(pin):
        return 'XYZ' if pin == 'Normal' else 'RGB'

    def out_pin(s, plug):
        """Graph output pin of a Maya plug: the output itself or one of its channels (Color.R)."""
        node, attr = plug.split('.', 1)
        for pin, a in s['outs'].items():
            if a == attr:
                return pin
            whole = node + '.' + a
            for k, kid in enumerate(_children(whole) if _is_compound3(whole) else []):
                if kid.split('.', 1)[1] == attr:
                    return pin + '.' + letters(pin)[k]
        return None

    def visit(mnode, d):
        if mnode in ids:
            depth[mnode] = max(depth[mnode], d)
            return ids[mnode]
        mtype = cmds.nodeType(mnode)
        if mtype not in rev:
            if mtype != 'place2dTexture' and not any(u['node'] == mnode for u in unknown):
                unknown.append({'node': mnode, 'type': mtype})
            return None
        t, s = rev[mtype]
        nid = len(ids) + 1
        ids[mnode] = nid
        depth[mnode] = d
        entry = {'id': nid, 'type': t, 'x': 0, 'y': 0, 'in': {}}
        nodes.append(entry)
        for pin, e in s['ins'].items():
            attrs = e.get('attrs') or ([e['attr']] if e.get('attr') else [])
            if not attrs:
                continue
            plug = _resolve(mnode, attrs[0])
            if not plug or not cmds.objExists(plug):
                continue
            if 'attrs' not in e:
                kids = _children(plug)[:3] if _is_compound3(plug) else []
                targets = [(plug, pin)] + [(kid, pin + '.' + letters(pin)[k]) for k, kid in enumerate(kids)]
                for target, pin_name in targets:
                    for src in cmds.listConnections(target, s=True, d=False, p=True) or []:
                        snode = src.split('.')[0]
                        sid = visit(snode, d + 1)
                        if sid is None:
                            continue
                        so = out_pin(rev[cmds.nodeType(snode)][1], src)
                        if so:
                            links.append({'from': [sid, so], 'to': [nid, pin_name]})
            if e.get('link_only'):
                continue
            try:
                v = cmds.getAttr(plug)
            except (RuntimeError, ValueError):
                continue
            if isinstance(v, list) and v and isinstance(v[0], tuple):
                v = list(v[0])
            if isinstance(v, (int, float, str, bool, list)):
                entry['in'][pin] = _transform(v, e, inverse=True)
        return nid

    visit(sg, 0)
    # layout: one column per depth, the Material Output on the right
    cols = {}
    for mnode, nid in ids.items():
        cols.setdefault(depth[mnode], []).append(nid)
    for d, col in cols.items():
        for i, nid in enumerate(col):
            nodes[nid - 1]['x'] = 900 - d * 320
            nodes[nid - 1]['y'] = i * 340 - (len(col) - 1) * 170
    shader = (cmds.listConnections(sg + '.surfaceShader', s=True, d=False) or [sg])[0]
    have = graphs()
    base = _clean(shader) + '_wzx'
    key, i = base, 1
    while key in have:
        i += 1
        key = '%s%d' % (base, i)
    data = {'format': 'wzx-graph', 'version': 1, 'name': key, 'nodes': nodes, 'links': links, 'groups': []}
    sync(data, key)
    result = {'key': key, 'nodes': len(nodes), 'unknown': unknown}
    if out_path:
        with open(out_path, 'w', encoding='utf-8') as f:
            json.dump(result, f)
    msg = 'converted %s: %d node(s)' % (shader, len(nodes))
    if unknown:
        msg += ', no rule for: ' + ', '.join('%s (%s)' % (u['node'], u['type']) for u in unknown)
        cmds.warning('WZX shader graph: ' + msg)
    return msg
