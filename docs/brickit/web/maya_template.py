# -*- coding: utf-8 -*-
# BrickIt Builder -> Maya : briques, shaders et mécanisme riggé.
# Utilisation : Maya > Script Editor > onglet Python > File > Load Script (ce fichier) > Execute (Ctrl+Entrée).
# Unités : centimètres, Y vers le haut. Le contrôleur BRICKIT_CTRL porte une commande par moteur / vérin.
import base64, json, zlib, math
import maya.cmds as cmds
import maya.api.OpenMaya as om

DATA = json.loads(zlib.decompress(base64.b64decode("__DATA__")).decode("utf-8"))
SCENE = DATA["name"]

def _clean(s):
    out = "".join(c if c.isalnum() else "_" for c in s)
    return out if out and not out[0].isdigit() else "p_" + out

def _srgb_to_linear(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

def make_shaders():
    arnold = False
    try:
        if not cmds.pluginInfo("mtoa", q=True, loaded=True):
            cmds.loadPlugin("mtoa", quiet=True)
        arnold = cmds.pluginInfo("mtoa", q=True, loaded=True)
    except Exception:
        arnold = False
    has_std = "standardSurface" in (cmds.allNodeTypes() or [])
    sgs = {}
    round_node = None
    if arnold:
        try:
            round_node = cmds.shadingNode("aiRoundCorners", asUtility=True, name=SCENE + "_bevel")
            cmds.setAttr(round_node + ".radius", DATA["bevel"])
        except Exception:
            round_node = None
    for cid, c in DATA["colors"].items():
        nm = _clean("brk_" + c["name"])
        kind = "aiStandardSurface" if arnold else ("standardSurface" if has_std else "blinn")
        sh = cmds.shadingNode(kind, asShader=True, name=nm)
        rgb = [_srgb_to_linear(x) for x in c["rgb"]]
        if kind == "blinn":
            cmds.setAttr(sh + ".color", *rgb, type="double3")
            if c["group"] == "transparent":
                cmds.setAttr(sh + ".transparency", 0.8, 0.8, 0.8, type="double3")
        else:
            cmds.setAttr(sh + ".baseColor", *rgb, type="double3")
            cmds.setAttr(sh + ".specularRoughness", 0.3)
            cmds.setAttr(sh + ".coat", 0.35)
            cmds.setAttr(sh + ".coatRoughness", 0.25)
            if c["group"] == "transparent":
                cmds.setAttr(sh + ".transmission", 0.92)
                cmds.setAttr(sh + ".transmissionColor", *rgb, type="double3")
                cmds.setAttr(sh + ".specularIOR", 1.49)
                if arnold:
                    try: cmds.setAttr(sh + ".thinWalled", 0)
                    except Exception: pass
            elif c["group"] == "metallic":
                cmds.setAttr(sh + ".metalness", 1.0)
                cmds.setAttr(sh + ".specularRoughness", 0.22)
            if round_node:
                try: cmds.connectAttr(round_node + ".outValue", sh + ".normalCamera", force=True)
                except Exception: pass
        sg = cmds.sets(renderable=True, noSurfaceShader=True, empty=True, name=nm + "SG")
        cmds.connectAttr(sh + ".outColor", sg + ".surfaceShader", force=True)
        sgs[cid] = sg
    return sgs

def make_part_meshes(lib_grp):
    protos = {}
    for name, m in DATA["meshes"].items():
        v = m["v"]; idx = m["i"]
        pts = [om.MPoint(v[k], v[k + 1], v[k + 2]) for k in range(0, len(v), 3)]
        fn = om.MFnMesh()
        obj = fn.create(pts, [3] * (len(idx) // 3), idx)
        path = om.MFnDagNode(obj).fullPathName()
        tr = cmds.rename(path, _clean("part_" + name))
        cmds.polySoftEdge(tr, angle=180, ch=False)   # creases are real vertex splits in the LDraw / BrickIt meshes
        cmds.parent(tr, lib_grp)
        protos[name] = tr
    cmds.setAttr(lib_grp + ".visibility", 0)
    return protos

def build():
    if cmds.objExists(SCENE):
        cmds.delete(SCENE)
    root = cmds.group(empty=True, name=SCENE)
    lib = cmds.group(empty=True, name=SCENE + "_pieces_ref", parent=root)
    sgs = make_shaders()
    protos = make_part_meshes(lib)
    ctrl = cmds.spaceLocator(name="BRICKIT_CTRL")[0]
    cmds.parent(ctrl, root)
    cmds.setAttr(ctrl + ".localScale", 2, 2, 2)
    # bodies and joints: every rigid body is a group; a joint is base group (rest pose) + driven group, pivot on the axle
    body_grp = {}
    joint_of_child = {j["child"]: j for j in DATA["joints"]}
    order = []
    def visit(b):
        order.append(b)
        for j in DATA["joints"]:
            if j["parent"] == b:
                visit(j["child"])
    for r in DATA["roots"]:
        visit(r)
    jn = {}
    for b in order:
        j = joint_of_child.get(b)
        parent = root
        if j is not None:
            par = body_grp[j["parent"]]
            def piv(g):
                cmds.xform(g, rotatePivot=j["p"], scalePivot=j["p"], objectSpace=True)
                return g
            if j.get("align"):
                par = piv(cmds.group(empty=True, name="J%d_axe" % j["k"], parent=par))
                cmds.setAttr(par + ".rotate", *j["align"], type="double3")
            base = piv(cmds.group(empty=True, name="J%d_%s_repos" % (j["k"], j["type"]), parent=par))
            drv = piv(cmds.group(empty=True, name="J%d_%s" % (j["k"], j["type"]), parent=base))
            if abs(j["base"]) > 1e-9:
                cmds.setAttr("%s.%s" % (base, j["chan"]), j["base"])
            jn[j["k"]] = drv
            parent = drv
            if j.get("unalign"):
                parent = piv(cmds.group(empty=True, name="J%d_axe_inv" % j["k"], parent=drv))
                cmds.setAttr(parent + ".rotate", *j["unalign"], type="double3")
        body_grp[b] = cmds.group(empty=True, name="corps_%d" % b, parent=parent)
    # bricks
    for i, br in enumerate(DATA["bricks"]):
        dup = cmds.duplicate(protos[br["part"]], name=_clean("%s_%d" % (br["part"], i)))[0]
        cmds.parent(dup, body_grp[br["body"]])
        cmds.xform(dup, matrix=br["m"], objectSpace=True)
        cmds.setAttr(dup + ".visibility", 1)
        cmds.sets(dup, edit=True, forceElement=sgs[str(br["color"])])
        cmds.addAttr(dup, longName="ldraw", dataType="string")
        cmds.setAttr(dup + ".ldraw", br["ldraw"], type="string")
    # commands -> set driven keys on the joints (sampled from the BrickIt kinematic solver, loops included)
    for c in DATA["cmds"]:
        at = c["attr"]
        if c["type"] == "motor":
            cmds.addAttr(ctrl, longName=at, niceName=c["name"], attributeType="double", defaultValue=0, keyable=True)
        else:
            cmds.addAttr(ctrl, longName=at, niceName=c["name"] + " (cm)", attributeType="double", defaultValue=0, minValue=c["lo"], maxValue=c["hi"], keyable=True)
    for j in DATA["joints"]:
        drv = jn[j["k"]]
        for ci, keys in j.get("sdk", {}).items():
            c = DATA["cmds"][int(ci)]
            for dv, val in keys:
                cmds.setDrivenKeyframe("%s.%s" % (drv, j["chan"]), currentDriver="%s.%s" % (ctrl, c["attr"]), driverValue=dv, value=val,
                                       inTangentType="linear" if c["type"] == "motor" else "spline",
                                       outTangentType="linear" if c["type"] == "motor" else "spline")
            if c["type"] == "motor":
                for crv in cmds.listConnections("%s.%s" % (drv, j["chan"]), source=True, destination=False, type="animCurve") or []:
                    cmds.setInfinity(crv, preInfinite="linear", postInfinite="linear")
                bw = cmds.listConnections("%s.%s" % (drv, j["chan"]), source=True, destination=False, type="blendWeighted") or []
                for b in bw:
                    for crv in cmds.listConnections(b, source=True, destination=False, type="animCurve") or []:
                        cmds.setInfinity(crv, preInfinite="linear", postInfinite="linear")
    # demo animation: each command goes to its max, its min, then back to 0
    t = 1
    for c in DATA["cmds"]:
        lo, hi = (-180.0, 180.0) if c["type"] == "motor" else (c["lo"], c["hi"])
        if hi - lo < 1e-6:
            continue
        for dt, v in ((0, 0.0), (30, hi), (70, lo), (100, 0.0)):
            cmds.setKeyframe(ctrl, attribute=c["attr"], time=t + dt, value=v)
        t += 100
    if DATA["cmds"]:
        cmds.playbackOptions(minTime=1, maxTime=t, animationStartTime=1, animationEndTime=t)
    cmds.select(ctrl)
    print("BrickIt : %d briques, %d corps, %d liaisons, %d commandes sur BRICKIT_CTRL" % (len(DATA["bricks"]), len(order), len(DATA["joints"]), len(DATA["cmds"])))

build()
