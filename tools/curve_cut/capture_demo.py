# -*- coding: utf-8 -*-
"""Demo + captures d'ecran reelles de curve_cut dans Maya (GUI).

Lancement (depuis le dossier qui contient curve_cut.py et ce fichier) :
    maya.exe -command "python(\"exec(open(r'CHEMIN\\capture_demo.py').read())\")"
ou, dans le Script Editor (Python) : exec(open(r'CHEMIN\\capture_demo.py').read())

Les PNG sont ecrits dans <dossier>/captures/, avec un log.txt.
Mettre CURVECUT_QUIT=1 dans l'environnement pour quitter Maya a la fin.
"""
import os
import sys
import math
import traceback

import maya.cmds as cmds
import maya.utils

HERE = os.path.dirname(os.path.abspath(globals().get("__file__", "") or os.getcwd()))
if not os.path.exists(os.path.join(HERE, "curve_cut.py")):
    HERE = os.environ.get("CURVECUT_DIR", os.getcwd())
OUT = os.path.join(HERE, "captures")
if not os.path.isdir(OUT):
    os.makedirs(OUT)
LOG = open(os.path.join(OUT, "log.txt"), "w")


def log(msg):
    LOG.write(msg + "\n")
    LOG.flush()
    print("[capture] " + msg)


def qt_grab(widget_name_or_ptr, path):
    """Capture d'un widget Maya (fenetre UI ou fenetre principale)."""
    import maya.OpenMayaUI as omui
    try:
        from shiboken6 import wrapInstance
        from PySide6 import QtWidgets
    except ImportError:
        from shiboken2 import wrapInstance
        from PySide2 import QtWidgets
    if widget_name_or_ptr == "main":
        ptr = omui.MQtUtil.mainWindow()
    else:
        ptr = omui.MQtUtil.findWindow(widget_name_or_ptr) or omui.MQtUtil.findControl(widget_name_or_ptr)
    w = wrapInstance(int(ptr), QtWidgets.QWidget)
    QtWidgets.QApplication.processEvents()
    w.grab().save(path)
    log("capture Qt : " + path)


def viewport(path, cam="persp", w=1280, h=760):
    panel = "modelPanel4"
    try:
        cmds.modelPanel(panel, e=True, camera=cam)
        cmds.modelEditor(panel, e=True, displayAppearance="smoothShaded",
                         wireframeOnShaded=False, grid=False, headsUpDisplay=False,
                         selectionHiliteDisplay=False)
    except Exception:
        pass
    cmds.setFocus(panel)
    cmds.refresh(force=True)
    base = os.path.splitext(path)[0]
    cmds.setAttr("defaultRenderGlobals.imageFormat", 32)  # png
    res = cmds.playblast(frame=[1], format="image", compression="png",
                         completeFilename=base + ".png", viewer=False,
                         showOrnaments=False, widthHeight=(w, h), percent=100,
                         offScreen=True, forceOverwrite=True)
    log("viewport : " + str(res))


def color(node, rgb):
    sh = cmds.shadingNode("lambert", asShader=True, name=node.split("|")[-1] + "_mat")
    cmds.setAttr(sh + ".color", *rgb, type="double3")
    sg = cmds.sets(renderable=True, noSurfaceShader=True, empty=True, name=sh + "SG")
    cmds.connectAttr(sh + ".outColor", sg + ".surfaceShader")
    cmds.sets(node, e=True, forceElement=sg)


def run():
    sys.path.insert(0, HERE)
    import curve_cut
    try:
        import importlib
        importlib.reload(curve_cut)
    except Exception:
        pass
    log("Maya %s, curve_cut %s" % (cmds.about(version=True), curve_cut.__version__))

    cmds.file(new=True, force=True)
    # objet de demo : un vase par revolution d'une courbe
    prof = []
    for i in range(21):
        t = i / 20.0
        y = -6 + 12 * t
        r = 2.8 + 1.5 * math.sin(t * math.pi * 1.6 + 0.3) + 0.6 * (1 - t)
        prof.append((r, y, 0))
    crv = cmds.curve(degree=3, point=[(0, -6, 0)] + prof + [(0, 6, 0)])
    vase = cmds.revolve(crv, axis=(0, 1, 0), polygon=1, sections=48,
                        constructionHistory=False, name="vase")[0]
    cmds.delete(crv)
    cmds.polyMergeVertex(vase, distance=0.001, constructionHistory=False)
    cmds.polyNormal(vase, normalMode=2, userNormalMode=0, constructionHistory=False)
    cmds.polyNormal(vase, normalMode=4, constructionHistory=False)  # unify
    log("vase : %d faces" % cmds.polyEvaluate(vase, face=True))

    cam = "persp"
    cmds.xform(cam, worldSpace=True, translation=(3, 2, 30), rotation=(-3, 6, 0))
    cmds.viewFit(cam, vase, fitFactor=0.75)

    # trait de coupe : points dans le plan image de la camera
    proj = curve_cut.projector_from_camera(cam)
    _, _, dmin, dmax = curve_cut.view_bounds(curve_cut._mesh_points(vase), proj)
    (cu, cv), r, _, _ = curve_cut.view_bounds(curve_cut._mesh_points(vase), proj)
    d = 0.5 * (dmin + dmax)
    pts = []
    for i in range(61):
        t = i / 60.0
        u = cu - 1.4 * r + 2.8 * r * t
        v = cv + 0.12 * r + 0.12 * r * math.sin(t * 2 * math.pi * 1.2) - 0.1 * r * t
        pts.append(proj.from_image(cu + (u - cu) * 0.55, v, d))
    stroke = cmds.curve(degree=1, point=pts, name="curveCut_curve1")
    curve_cut.store_projector(stroke, proj)
    cmds.setAttr(stroke + ".overrideEnabled", 1)
    cmds.setAttr(stroke + ".overrideColor", 13)
    try:
        cmds.setAttr(cmds.listRelatives(stroke, shapes=True)[0] + ".lineWidth", 4)
    except Exception:
        pass

    curve_cut._settings.update({"gap": 0.1, "connector": True, "conn_clearance": 0.05})
    curve_cut.show_ui()
    cmds.select(vase, stroke)
    viewport(os.path.join(OUT, "01_trait.png"), cam)
    qt_grab("main", os.path.join(OUT, "00_maya_complet_avant.png"))
    qt_grab(curve_cut.WINDOW, os.path.join(OUT, "05_interface.png"))

    opts = dict(curve_cut._settings)
    opts.pop("keep_curve")
    parts = curve_cut.cut_with_curve(stroke, [vase], projector=proj, **opts)
    log("pieces : %s" % parts)
    if len(parts) != 2:
        log("ECHEC : %d pieces" % len(parts))
        return
    cmds.setAttr(stroke + ".visibility", 0)
    a, b = parts
    color(a, (0.95, 0.55, 0.25))
    color(b, (0.35, 0.6, 0.95))
    cmds.select(clear=True)
    viewport(os.path.join(OUT, "02_coupe_assemblee.png"), cam)

    # ecarter les pieces vers le haut / le bas
    cmds.move(0, 2.5, 0, a, relative=True)
    cmds.move(0, -2.5, 0, b, relative=True)
    cmds.xform(cam, worldSpace=True, translation=(22, 12, 22), rotation=(-20, 45, 0))
    cmds.viewFit(cam, [a, b], fitFactor=0.8)
    viewport(os.path.join(OUT, "03_pieces_ecartees.png"), cam)

    # vue depuis le dessous de A (pin) puis dessus de B (socket)
    cmds.xform(cam, worldSpace=True, translation=(10, -14, 10), rotation=(35, 45, 0))
    cmds.viewFit(cam, a, fitFactor=0.8)
    viewport(os.path.join(OUT, "04a_pin.png"), cam)
    cmds.xform(cam, worldSpace=True, translation=(10, 14, 10), rotation=(-40, 45, 0))
    cmds.viewFit(cam, b, fitFactor=0.8)
    viewport(os.path.join(OUT, "04b_socket.png"), cam)
    qt_grab("main", os.path.join(OUT, "06_maya_complet_apres.png"))
    log("OK")


def main():
    try:
        run()
    except Exception:
        log(traceback.format_exc())
    finally:
        LOG.close()
        if os.environ.get("CURVECUT_QUIT") == "1":
            cmds.quit(force=True)


maya.utils.executeDeferred(main)
