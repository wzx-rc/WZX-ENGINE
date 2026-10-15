"""Dockable Maya panel hosting WzxShaderGraph.exe (WZX node graph -> Arnold shading network).

show() opens the panel: a small bar (graph of the scene, New) and the graph editor window. The exe runs as a separate
process: it prints its window handle, the panel wraps it with QWindow.fromWinId / createWindowContainer, and the exe
talks back through Maya's commandPort (wzx_shader_bridge.sync_file). install_shelf() adds the shelf button.
"""
import ctypes
import os
import subprocess
import threading

import maya.cmds as cmds
import maya.OpenMayaUI as omui

try:
    from PySide6 import QtCore, QtGui, QtWidgets
    from shiboken6 import wrapInstance
except ImportError:   # Maya 2024 and older
    from PySide2 import QtCore, QtGui, QtWidgets
    from shiboken2 import wrapInstance

import wzx_shader_bridge as bridge

PORT = 7811
CONTROL = 'WzxShaderGraphPanel'
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))       # repository (tools/maya/scripts -> root)
EXE = os.path.join(ROOT, 'build', 'mayashader', 'WzxShaderGraph.exe')
_panel = None


def graph_dir():
    d = os.path.join(cmds.internalVar(userAppDir=True), 'wzx_shader')
    if not os.path.isdir(d):
        os.makedirs(d)
    return d


def graph_path(key):
    return os.path.join(graph_dir(), key + '.graph.json')


def open_port():
    name = ':%d' % PORT
    if not cmds.commandPort(name, q=True):
        cmds.commandPort(name=name, sourceType='python', echoOutput=False, bufferSize=1 << 16)


def _settings_file():
    """Theme settings of the WZX editor when it was built next to the sources (same look as the engine)."""
    for root in (ROOT, os.path.normpath(os.path.join(ROOT, '..', '..'))):
        p = os.path.join(root, 'build', 'bin', 'wzx_settings.ini')
        if os.path.isfile(p):
            return p
    return ''


class Panel(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super(Panel, self).__init__(parent)
        self.proc = None
        self.hwnd = 0
        self.container = None
        self.key = None
        lay = QtWidgets.QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(2)
        bar = QtWidgets.QHBoxLayout()
        bar.setContentsMargins(6, 3, 6, 0)
        bar.addWidget(QtWidgets.QLabel('Graph'))
        self.combo = QtWidgets.QComboBox()
        self.combo.setMinimumWidth(200)
        self.combo.activated.connect(self._picked)
        bar.addWidget(self.combo)
        b = QtWidgets.QPushButton('New')
        b.clicked.connect(self.new_graph)
        bar.addWidget(b)
        b = QtWidgets.QPushButton('Restart')
        b.setToolTip('Restart the graph editor')
        b.clicked.connect(lambda: self.start(self.key))
        bar.addWidget(b)
        bar.addStretch(1)
        self.status = QtWidgets.QLabel('')
        bar.addWidget(self.status)
        lay.addLayout(bar)
        self.body = QtWidgets.QVBoxLayout()
        lay.addLayout(self.body, 1)
        keys = self.refresh()
        self.start(keys[0] if keys else self._free_key())

    def refresh(self):
        keys = sorted(bridge.graphs().keys())
        if self.key and self.key not in keys:
            keys.append(self.key)
        self.combo.clear()
        self.combo.addItems(keys)
        if self.key in keys:
            self.combo.setCurrentIndex(keys.index(self.key))
        return keys

    def _free_key(self):
        have = set(bridge.graphs().keys())
        i = 1
        while 'wzxMaterial%d' % i in have:
            i += 1
        return 'wzxMaterial%d' % i

    def new_graph(self):
        self.start(self._free_key())

    def _picked(self, i):
        k = self.combo.itemText(i)
        if k and k != self.key:
            self.start(k)

    def start(self, key):
        self.stop()
        self.key = key
        path = graph_path(key)
        stored = bridge.graphs().get(key)
        if stored:   # the scene is the reference: its copy of the graph is opened
            with open(path, 'w', encoding='utf-8') as f:
                f.write(stored)
        elif os.path.exists(path):
            os.remove(path)   # a graph of another scene with the same name: start from the default graph
        self.refresh()
        if not os.path.isfile(EXE):
            self.status.setText('Build missing: ' + EXE)
            return
        args = [EXE, '--embed', '--port', str(PORT), '--graph', path, '--maya-pid', str(os.getpid())]
        cm = cmds.colorManagementPrefs(q=True, cmEnabled=True)
        args += ['--rendering-space', cmds.colorManagementPrefs(q=True, renderingSpaceName=True) if cm else 'linear']
        s = _settings_file()
        if s:
            args += ['--settings', s]
        self.proc = subprocess.Popen(args, stdout=subprocess.PIPE, cwd=os.path.dirname(EXE),
                                     creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        line = []
        t = threading.Thread(target=lambda: line.append(self.proc.stdout.readline()))
        t.daemon = True
        t.start()
        t.join(15.0)
        text = line[0].decode('ascii', 'ignore').strip() if line else ''
        if not text.startswith('HWND '):
            self.status.setText('Graph editor did not start')
            return
        self.hwnd = int(text.split()[1])
        win = QtGui.QWindow.fromWinId(self.hwnd)
        self.container = QtWidgets.QWidget.createWindowContainer(win, self)
        self.container.setFocusPolicy(QtCore.Qt.StrongFocus)
        self.container.setMinimumSize(400, 300)
        self.body.addWidget(self.container)
        self.status.setText('%s  (pid %d)' % (key, self.proc.pid))

    def stop(self):
        if self.proc and self.proc.poll() is None:
            if self.hwnd:
                ctypes.windll.user32.PostMessageW(ctypes.c_void_p(self.hwnd), 0x0010, 0, 0)   # WM_CLOSE
            try:
                self.proc.wait(3)
            except subprocess.TimeoutExpired:
                self.proc.kill()   # only the process this panel started
        self.proc = None
        self.hwnd = 0
        if self.container:
            try:   # Qt may already have deleted the container with the foreign window
                self.body.removeWidget(self.container)
                self.container.deleteLater()
            except RuntimeError:
                pass
            self.container = None


def build_ui():
    global _panel
    ptr = omui.MQtUtil.findControl(CONTROL)
    if not ptr:
        return
    parent = wrapInstance(int(ptr), QtWidgets.QWidget)
    if _panel is not None:
        try:
            _panel.stop()
        except RuntimeError:
            pass
    _panel = Panel(parent)
    if parent.layout() is None:
        QtWidgets.QVBoxLayout(parent).setContentsMargins(0, 0, 0, 0)
    parent.layout().addWidget(_panel)


def on_close():
    global _panel
    if _panel is not None:
        _panel.stop()
        _panel = None


def show():
    open_port()
    bridge._plugins()
    on_close()
    if cmds.workspaceControl(CONTROL, exists=True):
        cmds.deleteUI(CONTROL)
    cmds.workspaceControl(CONTROL, label='WZX Shader Graph', floating=True, retain=False,
                          initialWidth=1400, initialHeight=780, minimumWidth=600,
                          uiScript='import wzx_shader_panel; wzx_shader_panel.build_ui()',
                          closeCommand='import wzx_shader_panel; wzx_shader_panel.on_close()')
    if _panel is None:
        build_ui()


def install_shelf(shelf='WZX_Shader'):
    """Adds the WZX Shader Graph button to a shelf (created when missing). Its own shelf: on Windows a
    "WZX" shelf would share the file shelf_Wzx.mel of an existing "Wzx" shelf."""
    top = __import__('maya.mel', fromlist=['eval']).eval('$tmp = $gShelfTopLevel')
    if not cmds.shelfLayout(shelf, exists=True):
        cmds.shelfLayout(shelf, parent=top)
    for b in cmds.shelfLayout(shelf, q=True, childArray=True) or []:
        if cmds.shelfButton(b, exists=True) and cmds.shelfButton(b, q=True, label=True) == 'WZX Shader Graph':
            cmds.deleteUI(b)
    cmd = ('import sys\np = r"%s"\nif p not in sys.path: sys.path.insert(0, p)\n'
           'import wzx_shader_panel\nwzx_shader_panel.show()' % HERE)
    cmds.shelfButton(parent=shelf, label='WZX Shader Graph', annotation='WZX Shader Graph: node graph for Arnold materials',
                     image='render_aiStandardSurface.png', imageOverlayLabel='WZX', sourceType='python', command=cmd)
    cmds.shelfTabLayout(top, e=True, selectTab=shelf)
    return shelf
