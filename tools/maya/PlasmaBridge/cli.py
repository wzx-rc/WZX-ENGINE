# -*- coding: utf-8 -*-
"""Plasma Bridge - conversion d'une scène Maya (.ma / .mb) pour l'Importer, sans ouvrir Maya.

  "C:\\Program Files\\Autodesk\\Maya2027\\bin\\mayapy.exe" cli.py "<scene.ma>" [--root <noeud>] [--inbox <dossier>]

Ouvre la scène (le fichier n'est jamais modifié), prend la brique (--root, sinon tous les groupes racine qui
contiennent des maillages), écrit le dossier d'envoi comme l'étagère (FBX + texture + manifest.json) et affiche
en dernière ligne :  PLASMA_RESULT {"folder": "...", "warnings": [...]}   ou   PLASMA_RESULT {"error": "..."} (code 1).
"""
import argparse
import json
import os
import sys


def _result(obj, code):
    sys.stdout.write('\nPLASMA_RESULT ' + json.dumps(obj, ensure_ascii=False) + '\n')
    sys.stdout.flush()
    sys.stderr.flush()
    # La fermeture normale de maya.standalone plante (et écrit une scène « Recovered » dans Temp) :
    # on termine le processus directement, sans passer par le nettoyage des DLL de Maya.
    if os.name == 'nt':
        import ctypes
        ctypes.windll.kernel32.TerminateProcess(ctypes.c_void_p(-1), code)  # -1 = ce processus
    os._exit(code)


def main():
    ap = argparse.ArgumentParser(description='Plasma Bridge : scène Maya -> boîte d\'envoi de l\'Importer')
    ap.add_argument('scene')
    ap.add_argument('--root', help='noeud de la brique (par défaut : toutes les racines avec des maillages)')
    ap.add_argument('--inbox', help='dossier de la boîte d\'envoi (par défaut celle de l\'app)')
    a = ap.parse_args()
    if not os.path.isfile(a.scene):
        _result({'error': u'Scène introuvable : %s' % a.scene}, 1)
    try:
        import maya.standalone
        maya.standalone.initialize(name='python')
    except Exception as e:
        _result({'error': u'Maya ne démarre pas (mayapy / licence) : %s' % e}, 1)
    try:
        import maya.cmds as cmds
        sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        from PlasmaBridge import core
        cmds.file(a.scene, open=True, force=True, ignoreVersion=True, prompt=False)
        if a.root:
            if not cmds.objExists(a.root):
                _result({'error': u'Noeud « %s » absent de la scène.' % a.root}, 1)
            sel = cmds.ls(a.root, long=True)
        else:
            sel = []
            for t in cmds.ls(assemblies=True, long=True):
                if cmds.listRelatives(t, allDescendents=True, type='mesh') or cmds.listRelatives(t, shapes=True, type='mesh'):
                    if not cmds.listRelatives(t, shapes=True, type='camera'):
                        sel.append(t)
        if not sel:
            _result({'error': u'Aucun maillage dans la scène.'}, 1)
        info = core.analyse(sel)
        if not a.root and len(sel) > 1:
            info['name'] = os.path.splitext(os.path.basename(a.scene))[0]
        folder = core.send(info, inbox=a.inbox, open_app=False)
        warnings = [msg for lvl, msg in info['issues'] if lvl in ('warn', 'bad')]
        _result({'folder': folder, 'warnings': warnings}, 0)
    except SystemExit:
        raise
    except Exception as e:
        _result({'error': u'Conversion impossible : %s' % e}, 1)


if __name__ == '__main__':
    main()
