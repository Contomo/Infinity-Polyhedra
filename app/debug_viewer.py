#!/usr/bin/env python3

from matplotlib.backends.qt_compat import QtWidgets

import re, colorsys, time
import numpy as np
import matplotlib
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Line3DCollection, Poly3DCollection
from matplotlib.widgets import Button



from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas

# choose backend
matplotlib.use("Qt5Agg")
#matplotlib.use("TkAgg")
# ── USER-TUNABLE CONSTANTS ────────────────────────────────────────────────
ELEV_LIMIT      = 35      # camera tilt clamp (±°)
REDRAW_DELAY    = 1/25    # seconds between forced redraws
SINGLE_ALPHA    = 0.75
GREY            = (0.5,)*3
LINE_W          = 2.5
LABEL_OFFSET    = 1.08
AXIS_LEN_FACTOR = 0.85
AXIS_OPACITY    = 0.4
LABEL_FONT_SIZE = 16
ZOOM_MARGIN    = 0.35   # smaller → more zoomed in (0.0…1.0)
CAMERA_DIST    = 5     # lower → camera nearer → more dramatic perspective
OPEN_WINDOW_SIZE = 6
_hsv = lambda h: colorsys.hsv_to_rgb(h,1,1)


def _parse(lines):
    """
    Parse a fresh geo-dump. Bei JEDEM Fehler: Exception werfen, damit das Modell
    komplett verworfen wird.
    """
    import re, logging
    V, E, F, H = [], [], [], []

    for line in lines:
        if line.startswith('V:'):
            parts = line[2:].strip().split(';')
            for part in parts:
                part = part.strip()
                if not part:
                    continue
                # Verteces: (x,y,z,h)
                try:
                    _, rest = part.split(',', 1)
                    x, y, z, h = map(float, rest.strip('()').split(','))
                    V.append((x, y, z))
                    H.append(h / 255.0)
                except ValueError as e:
                    logging.error(f"Vertex parsing error: {e} in part: {part}")
                    # sofort abbrechen
                    raise

        elif line.startswith('E:'):
            # Nur saubere (a-b)-Paare extrahieren
            for a_str, b_str in re.findall(r'\((\d+)-(\d+)\)', line):
                E.append((int(a_str), int(b_str)))
            # Wenn keine Paare gefunden wurden, war der Dump kaputt → abort
            if not E:
                raise ValueError(f"No valid edge pairs in line: {line!r}")

        elif line.startswith('f'):
            try:
                idxs = line.split(':', 1)[1]
                verts = [int(i) for i in idxs.strip().strip(',').split(',') if i]
                F.append(verts)
            except ValueError as e:
                logging.error(f"Face parsing error: {e} in line: {line}")
                raise

    return np.array(V), np.array(H), np.array(E), F


# ── viewer ────────────────────────────────────────────────────────────────
class Viewer:
    """3D polyhedron viewer with interactive rotation and mode switching."""
# ── geometry parser ──────────────────────────────────────────────────────
    
    def draw(self):
        self.fig.canvas.draw_idle()
        self.fig.canvas.flush_events()
        self._last_draw = time.time()  

    def __init__(self, V, H, E, F, figure=None):
        # Model data and initial states
        self.V, self.H, self.E, self.F = V, H, E, F
        self.mode = 'single'
        self.labels_on = True
        self.current_face = 0
        self._drag = False
        self._pending_face = None
        self._focused = True  # Track whether the figure is focused
        self._last_x = None
        self._last_y = None
        self._last_draw = 0

        # either embed into your Qt FigureCanvas, or fall back to a new window
        if figure is None:
            #plt.ion()
            self.fig = plt.figure('MCU poly', figsize=(OPEN_WINDOW_SIZE, OPEN_WINDOW_SIZE))
        else:
            self.fig = figure

        self.fig.patch.set_facecolor('#1a1a1a')
        fig = self.fig
        # 3D axis setup
        ax = fig.add_subplot(111, projection='3d', facecolor='#1a1a1a')
        ax.mouse_init(rotate_btn=None)
        ax.set_position([0, 0, 1, 1])  # Full figure coverage
        plt.subplots_adjust(left=0, right=1, bottom=0, top=1)
        ax.set_axis_off()
        ax.set_box_aspect((1, 1, 1))  # Equal aspect ratio for X, Y, Z

        # Set perspective projection with custom camera distance
        ax.set_proj_type('persp')
        ax.dist = CAMERA_DIST


        # Calculate center and radius for axis limits
        if V.size == 0:
            # Leeres Array: Default-Werte verwenden
            center = np.array([0.0, 0.0, 0.0])
            radius = 1.0  # Fallback-Radius, um Division durch Null zu vermeiden
        else:
            # Normale Berechnung, wenn V nicht leer ist
            center = V.mean(0)
            radius = np.ptp(V, 0).max() * ZOOM_MARGIN

        ax.set_xlim(center[0] - radius, center[0] + radius)
        ax.set_ylim(center[1] - radius, center[1] + radius)
        ax.set_zlim(center[2] - radius, center[2] + radius)

        # Axis arrows (X, Y, Z) with labels
        axis_length = radius * AXIS_LEN_FACTOR
        axis_opacity = AXIS_OPACITY  # 50% opacity (alpha)

        # Define axis vectors and labels (consistent lengths and positioning)
        axes = [
            ((axis_length, 0, 0), 'r', 'X'),
            ((0, axis_length, 0), 'g', 'Y'),
            ((0, 0, axis_length), 'b', 'Z')
        ]

        # Plot axis arrows and labels with 50% opacity
        for vector, color, label in axes:
            ax.quiver(0, 0, 0, *vector, color=color, lw=2, alpha=axis_opacity)
            ax.text(*vector, label, color=color, alpha=axis_opacity, ha='center', va='center', zdir='z')

        # Store figure and axis
        self.fig, self.ax = fig, ax

        # Build 3D collections (wires, faces, labels)
        self._build_collections()

        # Add buttons
        ax_btn_mode = fig.add_axes([0.78, 0.02, 0.2, 0.06])
        ax_btn_toggle = fig.add_axes([0.56, 0.02, 0.2, 0.06])
        self.btn_mode = Button(ax_btn_mode, 'Mode')
        self.btn_toggle = Button(ax_btn_toggle, 'Toggle labels')
        self.btn_mode.on_clicked(self._swap_mode)
        self.btn_toggle.on_clicked(self._toggle_labels)



        # Mouse and focus events
        fig.canvas.mpl_connect('button_press_event', self._on_press)
        fig.canvas.mpl_connect('button_release_event', self._on_release)
        fig.canvas.mpl_connect('motion_notify_event', self._on_motion)
        fig.canvas.mpl_connect('figure_enter_event', self._on_focus)
        fig.canvas.mpl_connect('figure_leave_event', self._on_unfocus)
        
        # Explicitly set to single-face mode and hide full-color edges
        self.full_coll.set_visible(False)
        self.active_coll.set_visible(True)

        # Initial face display
        self.show_face(0, first=True)
        #if self._focused:
        self.draw()



    def _build_collections(self):
        """Build 3D collections for wires, faces, and labels."""
        V, H, E = self.V, self.H, self.E
        ax = self.ax

        # Grey wireframe (static)
        wires = [[V[a], V[b]] for a, b in E]
        ax.add_collection3d(Line3DCollection(wires, colors=[GREY], lw=LINE_W/3))

        # Vertex points
        ax.scatter(V[:, 0], V[:, 1], V[:, 2], color=[GREY], s=12, depthshade=True)

        # Colored edges (dynamic)
        segsC, colsC = [], []
        for a, b in E:
            A, B = V[a], V[b]
            M = (A + B) / 2  # Midpoint for gradient effect
            segsC += [[A, M], [M, B]]
            colsC += [_hsv(H[a]), _hsv(H[b])]
        self.full_coll = ax.add_collection3d(
            Line3DCollection(segsC, colors=colsC, lw=LINE_W)
        )

        # Active edge collection (for single-face mode)
        dummy = [[V[0], V[0]]]
        self.active_coll = ax.add_collection3d(
            Line3DCollection(dummy, colors=[GREY], lw=LINE_W * 3, zorder=2)
        )

        # Wir legen für jedes Face einen eigenen Poly3DCollection-Patch an:
        self.face_patches = []
        for vs in self.F:
            if vs is None:
                self.face_patches.append(None)
                continue
            verts = [self.V[v] for v in vs]
            poly = Poly3DCollection(
                [verts],
                facecolors=(1,1,1,SINGLE_ALPHA),
                edgecolors=None,
                zsort='average',
                axlim_clip=False,
                zorder=1
            )
            poly.set_visible(False)
            ax.add_collection3d(poly)
            self.face_patches.append(poly)

        # Placeholder for vertex labels
        self.lbl = []
        # Damit self.patch überall weiter existiert:
        self.patch = self.face_patches[0]  # oder None, falls F[0] == None

    def _on_focus(self, event):
        """Event triggered when the window gains focus."""
        self._focused = True

    def _on_unfocus(self, event):
        """Event triggered when the window loses focus."""
        self._focused = False


    def _on_press(self, event):
        if event.button == 1 and event.inaxes is self.ax:
            self._drag = True
            # correctly initialize both coords
            self._last_x = event.x
            self._last_y = event.y

    def _on_release(self, event):
        if event.button == 1:
            self._drag = False
            self.draw()

    def _on_motion(self, event):
        now = time.time() 
        # bail out if we haven’t started a drag or moved outside the axes
        if not self._drag or event.inaxes is not self.ax or not self._focused:
        #if not self._focused or not self._drag:
            return
        


        if self._last_x is None or self._last_y is None:
            self._last_x = event.x
            self._last_y = event.y
            return
        
        
        #if now - self._last_draw < REDRAW_DELAY:
            #return  # zu früh, skip


        # now _last_x and _last_y are guaranteed to be ints
        dx = event.x - self._last_x
        dy = event.y - self._last_y

        # turntable rotation: only azim/elev, no roll
        new_az = self.ax.azim - dx * 0.15
        new_el = self.ax.elev - dy * 0.15
        new_el = max(-ELEV_LIMIT, min(ELEV_LIMIT, new_el))
        # lock the “up” vector by using view_init
        self.ax.view_init(elev=new_el, azim=new_az)
        # update for next motion event
        self._last_x = event.x
        self._last_y = event.y


        # throttle redraw to REDRAW_DELAY
        #if now - self._last_draw > REDRAW_DELAY:
        #self.draw()

        if now < self._last_draw + REDRAW_DELAY:
            return
        
        #self.fig.canvas.draw_idle()
        self.fig.canvas.flush_events()
        self._last_draw = time.time()  




    def _clear_labels(self):
        """Remove all existing vertex labels from the plot."""
        for label in self.lbl:
            label.remove()
        self.lbl.clear()

    def _add_label(self, idx, pos):
        """Add a vertex label that always sits in front of everything."""
        label = self.ax.text(
            *pos, f'V{idx}',
            color='w',
            fontsize=LABEL_FONT_SIZE,
            ha='center', va='center',
            visible=self.labels_on,
            zorder=999,       # draw last
            clip_on=False     # don’t clip against axes
        )
        self.lbl.append(label)

    def _make_face_labels(self, idx):
        """Clear existing labels and create new ones for the active face."""
        self._clear_labels()
        for vertex in self.F[idx]:
            self._add_label(vertex, self.V[vertex] * LABEL_OFFSET)

    def _swap_mode(self, _):
        """Toggle between 'full' and 'single' view modes."""
        # Switch mode
        self.mode = 'full' if self.mode == 'single' else 'single'
        
        # Update visibility based on mode
        self.full_coll.set_visible(self.mode == 'full')
        self.active_coll.set_visible(self.mode == 'single')
        self.patch.set_visible(True)

        # Clear old labels before switching modes
        self._clear_labels()

        # Rebuild labels for current mode if enabled
        if self.labels_on:
            if self.mode == 'full':
                for i, pos in enumerate(self.V):
                    self._add_label(i, pos * LABEL_OFFSET)
            else:
                self._make_face_labels(self.current_face)

        # Redraw the current face
        self.show_face(self.current_face)
        self.draw()

    def _toggle_labels(self, _):
        """Toggle visibility of vertex labels."""
        # Toggle the labels flag
        self.labels_on = not self.labels_on

        # Clear or rebuild labels based on the current mode and flag
        if self.labels_on:
            if self.mode == 'full':
                for i, pos in enumerate(self.V):
                    self._add_label(i, pos * LABEL_OFFSET)
            else:
                self._make_face_labels(self.current_face)
        else:
            self._clear_labels()

        self.draw()

    def show_face(self, idx, *, first=False):
        # 1) Gültigkeit prüfen
        if idx < 0 or idx >= len(self.F) or self.F[idx] is None:
            return

        # 2) Falls gerade Drag, auf später verschieben
        if self._drag:
            return

        # 3) Nur beim ersten Aufruf oder echtem Index-Wechsel weiter
        if not first and idx == getattr(self, 'current_face', None):
            return

        # 4) Alte Labels und alle Face-Patches ausblenden
        self._clear_labels()
        for poly in self.face_patches:
            if poly:
                poly.set_visible(False)

        # 5) Gewünschtes Face einblenden
        patch = self.face_patches[idx]
        patch.set_visible(True)
        self.patch = patch
        self.current_face = idx

        # 6) “Active” Kanten für dieses Face neu berechnen
        vs   = self.F[idx]
        segs = []
        cols = []
        cyc  = vs[1:] + vs[:1]
        for a, b in zip(vs, cyc):
            A, B = self.V[a], self.V[b]
            M     = (A + B) / 2
            segs += [[A, M], [M, B]]
            cols += [_hsv(self.H[a]), _hsv(self.H[b])]

        self.active_coll.set_segments(segs)
        self.active_coll.set_color(cols)
        self.active_coll.set_linewidth(LINE_W * 3)
        self.active_coll.set_visible(self.mode == 'single')

        # 7) Neue Labels zeichnen (nur im Single-Mode)
        if self.mode == 'single' and self.labels_on:
            self._make_face_labels(idx)

        # 8) Redraw nur, wenn nicht erster Aufruf und Fenster fokussiert
        if not first:
            self.draw()



    def update_geometry(self, lines):
        """
        Parse and rebuild geometry from a fresh dump, preserving mode and face,
        then redraw axis arrows without the surrounding box grid.
        """
        import logging

        # 1) Parse dump in temp; on error, keep old geometry
        try:
            V_new, H_new, E_new, F_new = _parse(lines)
        except Exception as exc:
            logging.error(f"update_geometry: parse error, keeping old geometry: {exc}")
            return

        # 2) Sanitize edges/faces
        maxv = len(V_new)
        E_new = np.array([(a,b) for (a,b) in E_new if 0<=a<maxv and 0<=b<maxv], dtype=int)
        F_new = [vs for vs in F_new if all(0<=v<maxv for v in vs)]

        # 3) Remove old collections and previous arrows/texts to avoid stacking
        for coll in list(self.ax.collections):
            coll.remove()
        # also remove any artists (e.g., quiver arrows) and text labels
        for art in list(self.ax.artists):
            art.remove()
        for txt in list(self.ax.texts):
            txt.remove()

        # 4) Update state
        self.V, self.H, self.E, self.F = V_new, H_new, E_new, F_new

        # 5) Rebuild collections and preserve mode
        self._build_collections()
        self.full_coll.set_visible(self.mode=='full')
        self.active_coll.set_visible(self.mode=='single')

        # 6) Show current face
        idx = getattr(self, 'current_face', 0)
        if not (0<=idx<len(self.F)):
            idx = 0
            self.current_face = 0
        self.show_face(idx, first=True)

        # 7) Axis arrows: recalc limits then draw arrows and labels
        center = self.V.mean(axis=0) if self.V.size else np.array([0.0,0.0,0.0])
        radius = np.ptp(self.V,0).max()*ZOOM_MARGIN if self.V.size else 1.0
        # set axis limits to preserve view
        self.ax.set_xlim(center[0]-radius, center[0]+radius)
        self.ax.set_ylim(center[1]-radius, center[1]+radius)
        self.ax.set_zlim(center[2]-radius, center[2]+radius)
        # draw semi-transparent axis arrows
        axis_len = radius * AXIS_LEN_FACTOR
        opacity  = AXIS_OPACITY
        for vec, col, lbl in [
            ((axis_len,0,0),'r','X'),
            ((0,axis_len,0),'g','Y'),
            ((0,0,axis_len),'b','Z')
        ]:
            self.ax.quiver(0,0,0,*vec, color=col, lw=2, alpha=opacity)
            self.ax.text(*vec, lbl, color=col, alpha=opacity,
                         ha='center', va='center', zdir='z')

        # 8) Final redraw
        #self.draw()
