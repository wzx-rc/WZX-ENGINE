# -*- coding: utf-8 -*-
"""Export d'une brique Plasma Builder depuis Maya (à lancer dans le Script Editor, onglet Python).

Sélectionne le groupe de la brique (ou ses objets) puis lance le script. Il écrit, dans le dossier choisi :
  <nom>_low.obj        les autres meshes (BASEMESH, LOW, RETOPO…)
  <nom>_high.obj       les meshes nommés comme un high pour l'importeur : HIGH, HD, DECIMATED, ZBRUSH, SCULPT… (facultatif)
  <nom>.sockets.json   les locators SOCKET_<type>_<nom> et PLUG_<type> (matrices monde, en cm)
Dépose ensuite les trois fichiers et la texture dans l'importeur (import.html).

Les positions restent celles de la scène : une brique modélisée sur l'ork dans la scène de BOY_32mm.mb retombe
exactement au même endroit dans le builder (placement « en place »).
Types de socket : head, grip, back, shoulder, chest, belt, base (ou any).

Sinon, un export FBX de la sélection (locators compris) se dépose directement dans l'importeur.
"""
import json
import os
import re

import maya.cmds as cmds


# Même règle que l'importeur (lodHint de js/importer.js) : DECIMATED est un high, BASEMESH un low.
_HIGH = re.compile(r'(^|[^a-z])(high|hi|hd|hp|decim\w*|zbrush|sculpt\w*|dense)([^a-z]|$)')


def _short(n):
    return n.split('|')[-1].split(':')[-1]


def _meshes_and_locators(roots):
    meshes, locs = [], []
    for r in roots:
        for n in [r] + (cmds.listRelatives(r, allDescendents=True, fullPath=True, type='transform') or []):
            shapes = cmds.listRelatives(n, shapes=True, fullPath=True, noIntermediate=True) or []
            if any(cmds.nodeType(s) == 'mesh' for s in shapes):
                meshes.append(n)
            elif any(cmds.nodeType(s) == 'locator' for s in shapes) and _short(n).upper().startswith(('SOCKET_', 'PLUG_')):
                locs.append(n)
    return meshes, locs


def _export_obj(nodes, path):
    cmds.select(nodes, replace=True)
    cmds.file(path, force=True, exportSelected=True, type='OBJexport', preserveReferences=False,
              options='groups=1;ptgroups=1;materials=0;smoothing=1;normals=1')


def export_brick(folder=None, name=None):
    sel = cmds.ls(selection=True, long=True)
    if not sel:
        cmds.error(u'Sélectionne la brique (groupe ou objets) avant de lancer le script.')
    cmds.loadPlugin('objExport', quiet=True)
    meshes, locs = _meshes_and_locators(sel)
    if not meshes:
        cmds.error(u'Aucun mesh dans la sélection.')
    name = name or _short(sel[0]).replace('_GRP', '').replace('_grp', '')
    if not folder:
        res = cmds.fileDialog2(fileMode=3, caption=u'Dossier de la brique')
        if not res:
            return
        folder = res[0]
    high = [m for m in meshes if _HIGH.search(_short(m).lower())]
    low = [m for m in meshes if m not in high]
    unit = cmds.currentUnit(query=True, linear=True)
    out = []
    _export_obj(low, os.path.join(folder, name + '_low.obj'))
    out.append(name + '_low.obj')
    if high:
        _export_obj(high, os.path.join(folder, name + '_high.obj'))
        out.append(name + '_high.obj')
    data = {'units': {'cm': 'cm', 'mm': 'mm', 'm': 'm'}.get(unit, 'cm'), 'locators': [
        {'name': _short(l), 'matrix': cmds.xform(l, query=True, worldSpace=True, matrix=True)} for l in locs]}
    with open(os.path.join(folder, name + '.sockets.json'), 'w') as f:
        json.dump(data, f, indent=2)
    out.append(name + '.sockets.json')
    cmds.select(sel, replace=True)
    print(u'Brique exportée dans %s : %s (%d locators)' % (folder, ', '.join(out), len(locs)))


if __name__ == '__main__':
    export_brick()
