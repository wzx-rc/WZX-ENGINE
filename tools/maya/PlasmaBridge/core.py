# -*- coding: utf-8 -*-
"""Plasma Bridge - coeur (sans interface) : analyse une brique dans la scène Maya et l'envoie à l'Importer.

Utilisé par la fenêtre (ui.py), l'étagère et la ligne de commande mayapy (cli.py, conversion des .ma).
Conventions de l'Importer de Plasma Orks Builder (js/importer.js, js/sockets.js) :
  - maillage dont le nom contient high / hd / decim... -> high ; low / base... / retopo... (ou rien) -> low
  - locators PLUG_<type> (le point posé sur le socket de l'ork) et SOCKET_<type>_<nom> (sockets offerts)
  - types : head, grip, back, shoulder, chest, belt, base, any
Un envoi écrit un dossier dans la boîte d'envoi (voir INBOX) : <nom>.fbx, la texture couleur, manifest.json (en dernier).
"""
import datetime
import json
import os
import re
import shutil
import subprocess

import maya.cmds as cmds

VERSION = '0.1.0'
TYPES = ['grip', 'head', 'back', 'shoulder', 'chest', 'belt', 'base', 'any']
TYPE_LABELS = {'grip': u'Main (arme)', 'head': u'Tête', 'back': u'Dos', 'shoulder': u'Épaule', 'chest': u'Torse',
               'belt': u'Ceinture', 'base': u'Socle', 'any': u'Libre'}
# catégorie de la bibliothèque du builder pour chaque type de plug (js/categories.js)
TYPE_TO_CATEGORY = {'grip': 'weapon', 'head': 'head', 'back': 'back', 'shoulder': 'shoulder', 'chest': 'chest',
                    'belt': 'belt', 'base': 'base', 'any': None}

INBOX = os.path.join(os.path.expanduser('~'), 'Documents', 'PlasmaOrks', 'Bridge', 'inbox')
APP_EXE = (r'C:\Users\wzx\Dropbox\WZX\WORKS\TOOLS RD\VIEWPORT3D\PlasmaOrksBuilder\app-v6'
           r'\Plasma Orks Builder-win32-x64\Plasma Orks Builder.exe')

_HIGH = re.compile(r'(^|[^a-z])(high|hi|hd|hp|decim\w*|zbrush|sculpt\w*|dense)([^a-z]|$)')
_LOW = re.compile(r'(^|[^a-z])(low|lo|lp|base\w*|retopo\w*)([^a-z]|$)')
_LOC = re.compile(r'^(SOCKET|PLUG)_([A-Za-z]+)(?:_(.+))?$')


def short(n):
    return n.split('|')[-1].split(':')[-1]


def lod_hint(name):
    """Même règle que l'Importer : 'high', 'low' ou None."""
    n = name.lower()
    if _HIGH.search(n):
        return 'high'
    if _LOW.search(n):
        return 'low'
    return None


def parse_locator(name):
    """Comme parseLocatorName de l'Importer : None si le nom n'est pas lu."""
    m = _LOC.match(short(name))
    if not m:
        return None
    t = m.group(2).lower()
    return {'kind': 'plug' if m.group(1) == 'PLUG' else 'socket', 'type': t if t in TYPES else 'any',
            'name': m.group(3) or t}


def guess_locator(name):
    """Pour un locator au nom non reconnu (ex. Socket_01) : ce que l'Importer comprendrait, et le nom proposé."""
    n = short(name)
    m = re.match(r'(?i)^(socket|plug)_([A-Za-z]+)(?:_(.+))?$', n)
    if m and m.group(2).lower() in TYPES:  # bon nom, mauvaise casse : SOCKET_ / PLUG_ en majuscules
        kind, t = m.group(1).lower(), m.group(2).lower()
        return kind, t, '%s_%s%s' % (kind.upper(), t, '_' + m.group(3) if m.group(3) else '')
    low = n.lower()
    t = next((x for x in TYPES if x in low), None)
    if low.startswith('socket') and t:
        rest = re.sub(r'(?i)^socket_?' + t + r'_?', '', n) or t
        return 'socket', t, 'SOCKET_%s_%s' % (t, rest)
    return 'plug', t, None


# ---------------------------------------------------------------- analyse

def _roots(sel=None):
    sel = sel if sel is not None else cmds.ls(selection=True, long=True, transforms=True)
    roots = []
    for s in sel:
        if cmds.nodeType(s) != 'transform':
            p = cmds.listRelatives(s, parent=True, fullPath=True)
            s = p[0] if p else s
        if not any(s.startswith(r + '|') or s == r for r in roots):
            roots = [r for r in roots if not r.startswith(s + '|')] + [s]
    return roots


def _tris(mesh):
    try:
        return int(cmds.polyEvaluate(mesh, triangle=True))
    except Exception:
        return 0


def _textures(meshes):
    """Texture couleur des matériaux des maillages (baseColor / color), puis les autres fichiers."""
    color, other = [], []
    for m in meshes:
        shapes = cmds.listRelatives(m, shapes=True, fullPath=True, noIntermediate=True) or []
        sgs = set(cmds.listConnections(shapes, type='shadingEngine') or [])
        for sg in sgs:
            mats = cmds.listConnections(sg + '.surfaceShader') or []
            for mat in mats:
                for attr in ('baseColor', 'color', 'TEX_color_map', 'base_color'):
                    if not cmds.attributeQuery(attr, node=mat, exists=True):
                        continue
                    for f in cmds.listHistory(mat + '.' + attr, pruneDagObjects=True) or []:
                        if cmds.nodeType(f) == 'file':
                            p = cmds.getAttr(f + '.fileTextureName')
                            if p and p not in color:
                                color.append(p)
                for f in cmds.listHistory(mat, pruneDagObjects=True) or []:
                    if cmds.nodeType(f) == 'file':
                        p = cmds.getAttr(f + '.fileTextureName')
                        if p and p not in color and p not in other:
                            other.append(p)
    return color, other


def analyse(sel=None):
    """Décrit la brique sélectionnée : nom, maillages (low / high), locators, catégorie, textures, problèmes."""
    roots = _roots(sel)
    meshes, locs = [], []
    for r in roots:
        nodes = [r] + (cmds.listRelatives(r, allDescendents=True, fullPath=True, type='transform') or [])
        for n in nodes:
            shapes = cmds.listRelatives(n, shapes=True, fullPath=True, noIntermediate=True) or []
            kinds = set(cmds.nodeType(s) for s in shapes)
            if 'mesh' in kinds:
                meshes.append(n)
            elif 'locator' in kinds:
                locs.append(n)
    name = short(roots[0]) if roots else ''
    if len(roots) == 1 and meshes == [roots[0]]:
        name = re.sub(r'(?i)[_-]?(low|high|lo|hi|hd|decimated|basemesh)$', '', name) or name
    name = re.sub(r'(?i)_grp$', '', name)

    mesh_info = []
    for m in meshes:
        hint = lod_hint(short(m))
        mesh_info.append({'node': m, 'name': short(m), 'lod': hint or 'low', 'auto': hint is not None, 'tris': _tris(m)})
    if mesh_info and not any(x['auto'] for x in mesh_info) and len(mesh_info) == 2:
        heavy = max(mesh_info, key=lambda x: x['tris'])
        for x in mesh_info:
            x['lod'] = 'high' if x is heavy else 'low'

    loc_info = []
    for l in locs:
        p = parse_locator(l)
        if p:
            loc_info.append({'node': l, 'name': short(l), 'valid': True, 'kind': p['kind'], 'type': p['type'], 'fix': None})
        else:
            kind, t, fix = guess_locator(l)
            loc_info.append({'node': l, 'name': short(l), 'valid': False, 'kind': kind, 'type': t, 'fix': fix})

    plugs = [x for x in loc_info if x['valid'] and x['kind'] == 'plug']
    plug_type = plugs[0]['type'] if plugs else None
    color, other = _textures(meshes)

    issues = []
    if not roots:
        issues.append(('bad', u'Rien de sélectionné : sélectionne le groupe de la brique.'))
    elif not meshes:
        issues.append(('bad', u'Aucun maillage dans la sélection.'))
    if meshes and not any(x['lod'] == 'low' for x in mesh_info):
        issues.append(('warn', u'Pas de low : le high servira aussi en low.'))
    if meshes and not any(x['lod'] == 'high' for x in mesh_info):
        issues.append(('info', u'Pas de high : le low servira aussi en high et à l\'impression.'))
    for x in loc_info:
        if not x['valid']:
            issues.append(('warn', u'Locator « %s » ignoré par l\'Importer (nom attendu PLUG_<type> ou SOCKET_<type>_<nom>).' % x['name']))
    if len(plugs) > 1:
        issues.append(('warn', u'%d PLUG_ : un seul est utilisé.' % len(plugs)))
    if roots and meshes and not plugs:
        issues.append(('info', u'Pas de PLUG_ : placement « en place » (modélisée sur l\'ork) ou à l\'origine.'))
    for p in color:
        if not os.path.isfile(p):
            issues.append(('warn', u'Texture introuvable : %s' % p))
    if meshes and not color:
        issues.append(('info', u'Pas de texture couleur trouvée sur les matériaux.'))

    return {'roots': roots, 'name': name, 'meshes': mesh_info, 'locators': loc_info, 'plugType': plug_type,
            'category': TYPE_TO_CATEGORY.get(plug_type) if plug_type else None, 'textures': color, 'otherTextures': other,
            'units': cmds.currentUnit(query=True, linear=True), 'issues': issues, 'bbox': _bbox(meshes)}


def _bbox(meshes):
    if not meshes:
        return None
    b = cmds.exactWorldBoundingBox(meshes)
    return [round(b[3] - b[0], 3), round(b[4] - b[1], 3), round(b[5] - b[2], 3)]


# ---------------------------------------------------------------- actions sur la scène (annulables)

def rename_locator(node, new):
    return cmds.rename(node, new)


def apply_lod_names(info):
    """Renomme les maillages dont le low / high n'est pas lisible dans le nom (ajoute _low / _high)."""
    done = []
    for m in info['meshes']:
        if lod_hint(m['name']) != m['lod']:
            base = re.sub(r'(?i)[_-]?(low|high|lo|hi|hd)$', '', m['name'])
            done.append(cmds.rename(m['node'], '%s_%s' % (base, m['lod'])))
    return done


def add_plug(ptype='grip', parent=None):
    """Crée un locator PLUG_<type> au centre de la sélection (composants ou objets), rangé sous parent."""
    sel = cmds.ls(selection=True, flatten=True) or []
    pos = [0.0, 0.0, 0.0]
    if sel:
        b = cmds.exactWorldBoundingBox(sel)
        pos = [(b[0] + b[3]) / 2.0, (b[1] + b[4]) / 2.0, (b[2] + b[5]) / 2.0]
    loc = cmds.spaceLocator(name='PLUG_%s' % ptype)[0]
    cmds.xform(loc, worldSpace=True, translation=pos)
    if parent and cmds.objExists(parent):
        loc = cmds.parent(loc, parent)[0]
    cmds.select(loc, replace=True)
    return loc


# ---------------------------------------------------------------- envoi

def _safe(n):
    return re.sub(r'[^A-Za-z0-9_.-]+', '_', n).strip('_') or 'brique'


def export_fbx(nodes, path):
    cmds.loadPlugin('fbxmaya', quiet=True)
    mel = __import__('maya.mel', fromlist=['eval'])
    for c in ('FBXResetExport', 'FBXExportSmoothingGroups -v true', 'FBXExportSmoothMesh -v false',
              'FBXExportInputConnections -v false', 'FBXExportEmbeddedTextures -v false', 'FBXExportCameras -v false',
              'FBXExportLights -v false', 'FBXExportSkins -v false', 'FBXExportShapes -v false',
              'FBXExportUpAxis y', 'FBXExportInAscii -v false'):
        mel.eval(c)
    keep = cmds.ls(selection=True, long=True)
    cmds.select(nodes, replace=True)
    try:
        mel.eval('FBXExport -f "%s" -s' % path.replace('\\', '/'))
    finally:
        cmds.select(keep, replace=True) if keep else cmds.select(clear=True)


def send(info=None, name=None, category=None, inbox=None, open_app=True, extra_textures=False):
    """Écrit la brique dans la boîte d'envoi et (option) l'ouvre dans l'Importer. Rend le chemin du dossier."""
    info = info or analyse()
    if not info['meshes']:
        raise RuntimeError(u'Aucun maillage à envoyer.')
    name = _safe(name or info['name'])
    inbox = inbox or inbox_dir()
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    final = os.path.join(inbox, '%s_%s' % (name, stamp))
    tmp = os.path.join(inbox, '.tmp_%s_%s' % (name, stamp))
    os.makedirs(tmp)
    try:
        fbx = name + '.fbx'
        export_fbx(info['roots'], os.path.join(tmp, fbx))
        files = [fbx]
        texs = list(info['textures']) + (list(info['otherTextures']) if extra_textures else [])
        for i, p in enumerate(texs):
            if not os.path.isfile(p):
                continue
            ext = os.path.splitext(p)[1].lower()
            dst = os.path.basename(p) if i else '%s_basecolor%s' % (name, ext)
            if dst in files:
                dst = '%d_%s' % (i, dst)
            shutil.copy2(p, os.path.join(tmp, dst))
            files.append(dst)
        manifest = {
            'bridge': 1, 'version': VERSION, 'name': name,
            'category': category or info['category'], 'plugType': info['plugType'],
            'units': info['units'], 'files': files,
            'meshes': [{'name': m['name'], 'lod': m['lod'], 'tris': m['tris']} for m in info['meshes']],
            'locators': [{'name': l['name'], 'kind': l['kind'], 'type': l['type'], 'valid': l['valid']} for l in info['locators']],
            'bbox': info['bbox'],
            'warnings': [msg for lvl, msg in info['issues'] if lvl in ('warn', 'bad')],
            'source': {'scene': cmds.file(query=True, sceneName=True) or None, 'maya': cmds.about(version=True)},
            'created': datetime.datetime.now().isoformat(timespec='seconds'),
        }
        with open(os.path.join(tmp, 'manifest.json'), 'w', encoding='utf-8') as f:
            json.dump(manifest, f, indent=2, ensure_ascii=False)
        os.rename(tmp, final)
    except Exception:
        shutil.rmtree(tmp, ignore_errors=True)
        raise
    if open_app:
        open_in_app(final)
    return final


BRIDGE_JSON = os.path.join(os.environ.get('APPDATA', os.path.expanduser('~')), 'PlasmaOrksBuilder', 'bridge.json')


def _bridge_json():
    try:
        with open(BRIDGE_JSON, encoding='utf-8') as f:
            return json.load(f)
    except Exception:
        return {}


def inbox_dir():
    """Boîte d'envoi : celle annoncée par l'app (bridge.json), sinon Documents\\PlasmaOrks\\Bridge\\inbox."""
    return _bridge_json().get('inbox') or INBOX


def app_exe():
    """Exe de l'app : celui écrit par l'app au démarrage (bridge.json), sinon l'app-v6 installée."""
    try:
        with open(BRIDGE_JSON, encoding='utf-8') as f:
            exe = json.load(f).get('exe')
        if exe and os.path.isfile(exe):
            return exe
    except Exception:
        pass
    return APP_EXE if os.path.isfile(APP_EXE) else None


def open_in_app(folder=None):
    """Lance (ou réveille) Plasma Orks Builder, avec le dossier à importer s'il y en a un."""
    exe = app_exe()
    if not exe:
        cmds.warning(u'Plasma Orks Builder introuvable (ni %s ni %s).' % (BRIDGE_JSON, APP_EXE))
        return False
    args = [exe] + (['--plasma-import', folder] if folder else [])
    subprocess.Popen(args, cwd=os.path.dirname(exe), close_fds=True,
                     creationflags=getattr(subprocess, 'DETACHED_PROCESS', 0))
    return True


def open_inbox():
    d = inbox_dir()
    if not os.path.isdir(d):
        os.makedirs(d)
    os.startfile(d)
