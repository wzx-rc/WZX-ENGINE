# -*- coding: utf-8 -*-
"""Plasma Bridge - fenêtre (PySide6, Maya 2027) : vérifier la brique sélectionnée et l'envoyer à l'Importer.

    import PlasmaBridge.ui as pb; pb.show()
"""
import os

from PySide6 import QtCore, QtGui, QtWidgets

import maya.cmds as cmds

from . import core

HERE = os.path.dirname(os.path.abspath(__file__))
ICONS = os.path.join(HERE, 'icons')
WIN_NAME = 'PlasmaBridgeWindow'

C = {'bg': '#0e0f11', 'bar': '#121316', 'panel': '#16171a', 'raised': '#1e2024', 'chip': '#26282d', 'line': '#23252a',
     'line2': '#2a2c31', 'text': '#e6e6e8', 'text2': '#c9cace', 'muted': '#9a9ca3', 'accent': '#f0435a',
     'accent_bg': '#2a1418', 'ok': '#4fd18b', 'warn': '#f2c230', 'info': '#8fbbff', 'high': '#b08cff', 'low': '#5fb3ff'}
HEAD = 'Bahnschrift SemiBold Condensed'

QSS = """
QWidget#PB { background: %(bg)s; color: %(text)s; font-family: 'Segoe UI'; font-size: 12px; }
QLabel { color: %(text)s; background: transparent; }
QLabel[role="muted"] { color: %(muted)s; font-size: 11px; }
QLabel[role="h"] { color: %(text)s; }
QFrame#Card { background: %(panel)s; border: 1px solid %(line)s; border-radius: 10px; }
QFrame#Header { background: %(bar)s; border-bottom: 1px solid %(line)s; }
QFrame#Row { background: %(raised)s; border-radius: 7px; }
QLineEdit, QComboBox { background: %(raised)s; border: 1px solid %(line2)s; border-radius: 7px; padding: 5px 9px;
  color: %(text)s; min-height: 20px; selection-background-color: %(accent)s; }
QLineEdit:focus, QComboBox:focus { border-color: %(accent)s; }
QComboBox::drop-down { border: 0; width: 22px; }
QComboBox QAbstractItemView { background: %(raised)s; color: %(text)s; border: 1px solid %(line2)s;
  selection-background-color: %(accent_bg)s; selection-color: %(accent)s; }
QPushButton { background: %(raised)s; border: 1px solid %(line2)s; border-radius: 7px; padding: 6px 12px; color: %(text2)s; }
QPushButton:hover { background: %(chip)s; color: %(text)s; }
QPushButton:pressed { background: %(panel)s; }
QPushButton[kind="primary"] { background: %(accent)s; border: 0; color: #ffffff; padding: 10px 18px; border-radius: 9px; }
QPushButton[kind="primary"]:hover { background: #ff5a70; }
QPushButton[kind="primary"]:disabled { background: #4a2a30; color: #9a8a8e; }
QPushButton[kind="fix"] { background: %(accent_bg)s; border: 1px solid %(accent)s; color: %(accent)s; padding: 4px 10px; }
QPushButton[kind="chip"] { border-radius: 10px; padding: 2px 10px; font-weight: 600; font-size: 11px; }
QCheckBox { color: %(text2)s; spacing: 8px; }
QCheckBox::indicator { width: 15px; height: 15px; border-radius: 4px; border: 1px solid %(line2)s; background: %(raised)s; }
QCheckBox::indicator:checked { background: %(accent)s; border-color: %(accent)s; }
QScrollArea { border: 0; background: transparent; }
QScrollBar:vertical { background: transparent; width: 8px; }
QScrollBar::handle:vertical { background: %(chip)s; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QToolTip { background: %(raised)s; color: %(text)s; border: 1px solid %(line2)s; }
""" % dict(C, head=HEAD)


def _icon(name):
    p = os.path.join(ICONS, name)
    return QtGui.QIcon(p) if os.path.isfile(p) else QtGui.QIcon()


def head_font(px):
    """Bahnschrift condensée (police Windows), proche de la Barlow Condensed de l'app."""
    f = QtGui.QFont('Bahnschrift')
    f.setPixelSize(px)
    f.setWeight(QtGui.QFont.DemiBold)
    f.setStretch(QtGui.QFont.Condensed)
    f.setLetterSpacing(QtGui.QFont.AbsoluteSpacing, 0.6)
    return f


def _label(text, role=None):
    l = QtWidgets.QLabel(text)
    if role:
        l.setProperty('role', role)
    if role == 'h':
        l.setFont(head_font(16))
    return l


def _clear(layout):
    while layout.count():
        it = layout.takeAt(0)
        w = it.widget()
        if w:
            w.hide()
            w.setParent(None)
            w.deleteLater()
        elif it.layout():
            _clear(it.layout())


class Card(QtWidgets.QFrame):
    def __init__(self, title, right=None):
        super().__init__()
        self.setObjectName('Card')
        v = QtWidgets.QVBoxLayout(self)
        v.setContentsMargins(14, 12, 14, 14)
        v.setSpacing(8)
        h = QtWidgets.QHBoxLayout()
        h.addWidget(_label(title.upper(), 'h'))
        h.addStretch(1)
        if right:
            h.addWidget(right)
        v.addLayout(h)
        self.body = QtWidgets.QVBoxLayout()
        self.body.setSpacing(6)
        v.addLayout(self.body)


def _maya_main():
    try:
        from maya import OpenMayaUI as omui
        from shiboken6 import wrapInstance
        p = omui.MQtUtil.mainWindow()
        return wrapInstance(int(p), QtWidgets.QWidget) if p else None
    except Exception:
        return None


class BridgeWindow(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super().__init__(parent or _maya_main(), QtCore.Qt.Window)
        self.setObjectName('PB')
        self.setAttribute(QtCore.Qt.WA_StyledBackground, True)
        self.setWindowTitle(u'Plasma Bridge')
        self.setWindowIcon(_icon('plasma_bridge.png'))
        self.setStyleSheet(QSS)
        self.resize(470, 860)
        self.info = None
        self._job = None

        root = QtWidgets.QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # en-tête : logo, titre, état de l'app
        header = QtWidgets.QFrame()
        header.setObjectName('Header')
        hl = QtWidgets.QHBoxLayout(header)
        hl.setContentsMargins(16, 12, 16, 12)
        logo = QtWidgets.QLabel()
        logo.setPixmap(_icon('plasma_skull.png').pixmap(38, 38))
        hl.addWidget(logo)
        tv = QtWidgets.QVBoxLayout()
        tv.setSpacing(0)
        t = QtWidgets.QLabel(u'PLASMA BRIDGE')
        t.setFont(head_font(25))
        tv.addWidget(t)
        tv.addWidget(_label(u'Maya  →  Importer de Plasma Orks Builder', 'muted'))
        hl.addLayout(tv)
        hl.addStretch(1)
        self.appPill = QtWidgets.QLabel()
        hl.addWidget(self.appPill)
        root.addWidget(header)

        scroll = QtWidgets.QScrollArea()
        scroll.setWidgetResizable(True)
        inner = QtWidgets.QWidget()
        inner.setObjectName('PB')
        self.col = QtWidgets.QVBoxLayout(inner)
        self.col.setContentsMargins(14, 14, 14, 14)
        self.col.setSpacing(12)
        scroll.setWidget(inner)
        root.addWidget(scroll, 1)

        # brique
        refresh = QtWidgets.QPushButton(u'  Lire la sélection')
        refresh.setIcon(_icon('pb_refresh.png'))
        refresh.clicked.connect(self.refresh)
        self.cardBrick = Card(u'Brique', refresh)
        form = QtWidgets.QFormLayout()
        form.setLabelAlignment(QtCore.Qt.AlignLeft)
        form.setHorizontalSpacing(12)
        form.setVerticalSpacing(8)
        self.nameEdit = QtWidgets.QLineEdit()
        self.catCombo = QtWidgets.QComboBox()
        self.catCombo.addItem(u'Automatique (d\'après le PLUG)', None)
        for k, lab in [('weapon', u'Armes'), ('head', u'Têtes'), ('back', u'Dos'), ('shoulder', u'Épaulières'),
                       ('chest', u'Torse'), ('belt', u'Ceinture'), ('base', u'Socles')]:
            self.catCombo.addItem(lab, k)
        form.addRow(_label(u'Nom', 'muted'), self.nameEdit)
        form.addRow(_label(u'Catégorie', 'muted'), self.catCombo)
        self.sizeLbl = _label('', 'muted')
        form.addRow(_label(u'Taille', 'muted'), self.sizeLbl)
        self.cardBrick.body.addLayout(form)
        self.col.addWidget(self.cardBrick)

        self.cardMesh = Card(u'Maillages')
        self.col.addWidget(self.cardMesh)

        # locators + ajout d'un PLUG
        addBox = QtWidgets.QWidget()
        al = QtWidgets.QHBoxLayout(addBox)
        al.setContentsMargins(0, 0, 0, 0)
        al.setSpacing(6)
        self.plugType = QtWidgets.QComboBox()
        for tkey in core.TYPES[:-1]:
            self.plugType.addItem(core.TYPE_LABELS[tkey], tkey)
        add = QtWidgets.QPushButton(u'  PLUG')
        add.setIcon(_icon('pb_plug.png'))
        add.setToolTip(u'Crée un locator PLUG_<type> au centre de la sélection (sommets, faces ou objet)')
        add.clicked.connect(self.add_plug)
        al.addWidget(self.plugType)
        al.addWidget(add)
        self.cardLoc = Card(u'Locators', addBox)
        self.col.addWidget(self.cardLoc)

        self.cardCheck = Card(u'Contrôles')
        self.col.addWidget(self.cardCheck)
        self.col.addStretch(1)

        # pied : options + envoyer
        foot = QtWidgets.QFrame()
        foot.setObjectName('Header')
        fl = QtWidgets.QVBoxLayout(foot)
        fl.setContentsMargins(14, 12, 14, 14)
        fl.setSpacing(10)
        opts = QtWidgets.QHBoxLayout()
        self.openApp = QtWidgets.QCheckBox(u'Ouvrir dans l\'Importer')
        self.openApp.setChecked(True)
        opts.addWidget(self.openApp)
        opts.addStretch(1)
        inbox = QtWidgets.QPushButton(u'  Boîte d\'envoi')
        inbox.setIcon(_icon('pb_folder.png'))
        inbox.clicked.connect(core.open_inbox)
        opts.addWidget(inbox)
        fl.addLayout(opts)
        self.sendBtn = QtWidgets.QPushButton(u'  ENVOYER À L\'IMPORTER')
        self.sendBtn.setProperty('kind', 'primary')
        self.sendBtn.setIcon(_icon('pb_send.png'))
        self.sendBtn.setIconSize(QtCore.QSize(20, 20))
        self.sendBtn.setFont(head_font(19))
        self.sendBtn.clicked.connect(self.send)
        fl.addWidget(self.sendBtn)
        self.status = _label('', 'muted')
        self.status.setWordWrap(True)
        fl.addWidget(self.status)
        root.addWidget(foot)

        self.refresh()

    # ------------------------------------------------------------ état
    def _pill(self):
        exe = core.app_exe()
        ok = bool(exe)
        self.appPill.setText(u'  ●  App trouvée  ' if ok else u'  ●  App introuvable  ')
        self.appPill.setToolTip(exe or core.APP_EXE)
        col = C['ok'] if ok else C['warn']
        self.appPill.setStyleSheet('color: %s; background: %s; border: 1px solid %s; border-radius: 10px; padding: 3px 4px; font-size: 11px;'
                                   % (col, C['raised'], C['line2']))

    def refresh(self, *_):
        self._pill()
        try:
            self.info = core.analyse()
        except Exception as e:
            self.info = None
            self.status.setText(u'Analyse impossible : %s' % e)
            return
        i = self.info
        if not self.nameEdit.hasFocus():
            self.nameEdit.setText(i['name'])
        b = i['bbox']
        self.sizeLbl.setText(u'%.1f × %.1f × %.1f %s' % (b[0], b[1], b[2], i['units']) if b else u'—')
        if i['category']:
            k = self.catCombo.findData(i['category'])
            self.catCombo.setItemText(0, u'Automatique : %s' % (self.catCombo.itemText(k) if k > 0 else i['category']))
        else:
            self.catCombo.setItemText(0, u'Automatique (d\'après le PLUG)')

        # maillages
        _clear(self.cardMesh.body)
        if not i['meshes']:
            self.cardMesh.body.addWidget(_label(u'Sélectionne le groupe de la brique dans Maya.', 'muted'))
        for m in i['meshes']:
            self.cardMesh.body.addWidget(self._mesh_row(m))
        # locators
        _clear(self.cardLoc.body)
        if not i['locators']:
            self.cardLoc.body.addWidget(_label(u'Aucun locator. Une arme a besoin d\'un PLUG_grip (ou d\'être modélisée sur l\'ork).', 'muted'))
        for l in i['locators']:
            self.cardLoc.body.addWidget(self._loc_row(l))
        # contrôles
        _clear(self.cardCheck.body)
        if not i['issues']:
            self.cardCheck.body.addWidget(self._issue('ok', u'Tout est prêt pour l\'Importer.'))
        for lvl, msg in i['issues']:
            self.cardCheck.body.addWidget(self._issue(lvl, msg))
        if i['textures']:
            self.cardCheck.body.addWidget(self._issue('ok', u'Texture : %s' % os.path.basename(i['textures'][0])))
        self.sendBtn.setEnabled(bool(i['meshes']))

    def _row(self):
        f = QtWidgets.QFrame()
        f.setObjectName('Row')
        h = QtWidgets.QHBoxLayout(f)
        h.setContentsMargins(10, 6, 8, 6)
        h.setSpacing(8)
        return f, h

    def _mesh_row(self, m):
        f, h = self._row()
        n = QtWidgets.QLabel(m['name'])
        n.setStyleSheet('font-weight: 600;')
        h.addWidget(n)
        h.addWidget(_label(u'%s tri.' % format(m['tris'], ',').replace(',', ' '), 'muted'))
        h.addStretch(1)
        chip = QtWidgets.QPushButton(m['lod'].upper())
        chip.setProperty('kind', 'chip')
        col = C['high'] if m['lod'] == 'high' else C['low']
        chip.setStyleSheet('QPushButton { color: %s; border: 1px solid %s; background: transparent; }' % (col, col))
        chip.setToolTip(u'Lu dans le nom' if m['auto'] else u'Deviné (le nom ne dit ni low ni high) : clic pour l\'écrire dans le nom')
        chip.clicked.connect(lambda _=False, mm=m: self.toggle_lod(mm))
        h.addWidget(chip)
        return f

    def _loc_row(self, l):
        f, h = self._row()
        dot = QtWidgets.QLabel(u'●')
        dot.setStyleSheet('color: %s;' % (C['ok'] if l['valid'] else C['warn']))
        h.addWidget(dot)
        n = QtWidgets.QLabel(l['name'])
        n.setStyleSheet('font-weight: 600;')
        h.addWidget(n)
        if l['valid']:
            h.addWidget(_label(u'%s · %s' % ('Plug' if l['kind'] == 'plug' else 'Socket', core.TYPE_LABELS.get(l['type'], l['type'])), 'muted'))
            h.addStretch(1)
            if l.get('frozen'):
                dot.setStyleSheet('color: %s;' % C['warn'])
                b = QtWidgets.QPushButton(u'Dégeler')
                b.setProperty('kind', 'fix')
                b.setToolTip(u'Gelé (Freeze Transformations) : remet sa position dans la transformation, sans le déplacer à l\'écran')
                b.clicked.connect(lambda _=False, ll=l: self.unfreeze(ll))
                h.addWidget(b)
        else:
            h.addWidget(_label(u'ignoré', 'muted'))
            h.addStretch(1)
            if l['fix']:
                b = QtWidgets.QPushButton(u'→ ' + l['fix'])
                b.setProperty('kind', 'fix')
                b.clicked.connect(lambda _=False, ll=l, nn=l['fix']: self.rename(ll, nn))
                h.addWidget(b)
            else:
                combo = QtWidgets.QComboBox()
                for tkey in core.TYPES[:-1]:
                    combo.addItem('PLUG_' + tkey, 'PLUG_' + tkey)
                if l['type']:
                    combo.setCurrentIndex(max(0, combo.findData('PLUG_' + l['type'])))
                b = QtWidgets.QPushButton(u'Renommer')
                b.setProperty('kind', 'fix')
                b.clicked.connect(lambda _=False, ll=l, cc=combo: self.rename(ll, cc.currentData()))
                h.addWidget(combo)
                h.addWidget(b)
        return f

    def _issue(self, lvl, msg):
        f = QtWidgets.QWidget()
        h = QtWidgets.QHBoxLayout(f)
        h.setContentsMargins(0, 0, 0, 0)
        h.setSpacing(8)
        col = {'ok': C['ok'], 'warn': C['warn'], 'bad': C['accent'], 'info': C['info']}[lvl]
        mark = QtWidgets.QLabel({'ok': u'✔', 'warn': u'!', 'bad': u'✖', 'info': u'i'}[lvl])
        mark.setFixedWidth(16)
        mark.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignTop)
        mark.setStyleSheet('color: %s; font-weight: 700;' % col)
        t = QtWidgets.QLabel(msg)
        t.setWordWrap(True)
        t.setStyleSheet('color: %s;' % C['text2'])
        h.addWidget(mark)
        h.addWidget(t, 1)
        return f

    # ------------------------------------------------------------ actions
    def rename(self, l, new):
        cmds.undoInfo(openChunk=True, chunkName='PlasmaBridge_rename')
        try:
            core.rename_locator(l['node'], new)
        finally:
            cmds.undoInfo(closeChunk=True)
        self.status.setText(u'%s renommé en %s (Ctrl+Z pour annuler).' % (l['name'], new))
        self.refresh()

    def unfreeze(self, l):
        cmds.undoInfo(openChunk=True, chunkName='PlasmaBridge_unfreeze')
        try:
            core.unfreeze_locator(l['node'])
        finally:
            cmds.undoInfo(closeChunk=True)
        self.status.setText(u'%s dégelé : oriente-le (E) comme la brique doit être tenue, sans le geler (Ctrl+Z pour annuler).' % l['name'])
        self.refresh()

    def toggle_lod(self, m):
        m['lod'] = 'low' if m['lod'] == 'high' else 'high'
        cmds.undoInfo(openChunk=True, chunkName='PlasmaBridge_lod')
        try:
            core.apply_lod_names(self.info)
        finally:
            cmds.undoInfo(closeChunk=True)
        self.refresh()

    def add_plug(self):
        parent = self.info['roots'][0] if self.info and self.info['roots'] else None
        cmds.undoInfo(openChunk=True, chunkName='PlasmaBridge_plug')
        try:
            loc = core.add_plug(self.plugType.currentData(), parent)
        finally:
            cmds.undoInfo(closeChunk=True)
        self.status.setText(u'%s créé : place-le là où la brique se pose (W / E), puis « Lire la sélection » sur le groupe.' % core.short(loc))
        if parent:
            cmds.select(parent, replace=True)
        self.refresh()

    def send(self):
        self.refresh()
        if not self.info or not self.info['meshes']:
            return
        QtWidgets.QApplication.setOverrideCursor(QtCore.Qt.WaitCursor)
        try:
            folder = core.send(self.info, name=self.nameEdit.text().strip() or None,
                               category=self.catCombo.currentData(), open_app=self.openApp.isChecked())
        except Exception as e:
            self.status.setText(u'<span style="color:%s">Envoi impossible : %s</span>' % (C['accent'], e))
            return
        finally:
            QtWidgets.QApplication.restoreOverrideCursor()
        self.status.setText(u'Envoyé : %s' % folder)

    # suit la sélection de Maya
    def showEvent(self, e):
        super().showEvent(e)
        if self._job is None:
            try:
                self._job = cmds.scriptJob(event=['SelectionChanged', self.refresh], killWithScene=False)
            except Exception:
                self._job = None

    def closeEvent(self, e):
        if self._job is not None and cmds.scriptJob(exists=self._job):
            cmds.scriptJob(kill=self._job, force=True)
        self._job = None
        super().closeEvent(e)


_win = None


def show():
    global _win
    for w in QtWidgets.QApplication.topLevelWidgets():
        if w.objectName() == 'PB' and w is not _win:
            w.close()
    if _win is not None:
        try:
            _win.close()
            _win.deleteLater()
        except Exception:
            pass
    _win = BridgeWindow()
    _win.show()
    _win.raise_()
    return _win


def quick_send():
    """Bouton d'étagère « Envoi rapide » : envoie la sélection telle quelle, sans fenêtre."""
    info = core.analyse()
    bad = [m for lvl, m in info['issues'] if lvl == 'bad']
    if bad:
        cmds.warning(u'Plasma Bridge : ' + bad[0])
        return None
    folder = core.send(info)
    warns = [m for lvl, m in info['issues'] if lvl == 'warn']
    cmds.inViewMessage(amg=u'<span style="color:#f0435a">PLASMA</span> envoyé à l\'Importer' + (u' · %d alerte(s)' % len(warns) if warns else ''),
                       pos='topCenter', fade=True)
    for w in warns:
        cmds.warning(u'Plasma Bridge : ' + w)
    return folder
