"""Cardputer ADV hinged module case - v2 (cable trough, kickstand, cyberpunk styling).
All coordinates are the ASSEMBLED/CLOSED position, in the frame of the user's slip-in sleeve STL:
  x : -x = Cardputer face (lid side), +x = back (bottom module bay)
  y : length of the Cardputer
  z : up; Cardputer slides in from +z, hinge line runs along y at the top.
Parts: base, joint, lid, base_cover, lid_cover, kickstand.
Run:  RES=0.2 python3 build.py [part ...]
"""
import sys, os, time, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sdf import *

H = float(os.environ.get('RES', 0.2))
TEMPLATE = os.environ.get('TEMPLATE', '/home/claude/in/case.stl')
OUT = os.environ.get('OUTDIR', '/home/claude/out')

# ---------------- global ----------------
Y0, Y1 = -26.4, 62.5              # overall length
B = (4.9, 64.0)                   # lid <-> joint pivot (x, z)
A = (41.6, 64.0)                  # joint <-> base pivot
K = (55.6, 61.6)                  # kickstand pivot
RJ, RL, RK = 5.5, 5.6, 3.5        # joint radius, lid cap radius, kickstand tube radius
CLR_M3, PILOT_M3 = 1.7, 1.3       # M3 clearance / self-tap pilot radii
ARM0, ARM1 = (Y0, -21.6), (57.7, Y1)       # joint arms (pivot B)
EAR0, EAR1 = (Y0, -21.9), (58.0, Y1)       # base ears + kickstand lugs
JMID = (-21.6, 57.7)
LCAP = (-21.3, 57.4)
CABLE_Y = (-17.8, 53.9)
ARMOR = 1.2                       # raised "armor plate" thickness on the covers (print the top in a 2nd colour)
# ---------------- base / bay ----------------
BACK = 37.75                      # outer face of the sleeve back wall
X_SH = 47.1                       # shallow (NFC / RFID pocket) outer face
X_DP = 56.6                       # deep outer face (PaHub hump + cable trough)
COV_T = 1.6
BAY_IN = (-24.6, 60.7)
BAY_Z = (0.0, 57.8); BAY_ZIN = (1.8, 56.0)
HUMP_Y = (3.2, 35.8)              # PaHub bulge
TROUGH_Z = 22.0                   # full-depth cable trough along the connector edge (z 1.8 .. 22)
SCREWS = [(-21.6, 4.6), (57.7, 4.6)]
BAY_WIN = [(1.2, 8.6), (30.2, 36.6)]       # cable windows bay -> joint
# sleeve rework (v3)
KB_OPEN = dict(y=(-22.0, 58.3), ztop=35.0)        # keyboard opening: bottom lip + bar under the screen removed
RAIL_CUT_X = 14.3                                   # remove the old sliding rails/hooks in front of the frame
FLOOR = dict(x=(17.0, BACK), z=(0.0, 1.3))           # floor flush with the module bay
FLOOR_TRI = dict(y0=-15.5, n=7, base=13.0, step=9.8, x=(20.5, 34.5))   # finger push-out triangles
PEG_SHIFT = 3.6                                     # outer module peg pairs move this far towards the middle
SIDE_NOTCH = dict(x=(38.2, 44.6), z=(40.0, 52.0))   # Cardputer Grove cable entry, both ends
# ---------------- lid ----------------
LID_X = (-0.7, 10.5)
LID_PLATE = 8.9
LID_ZTOP, LID_END_TOP = 64.0, 58.0
TY, TZ = 118.35, 60.0             # template lid -> closed
IR_PILL = dict(y=(16.4, 40.4), z=(5.6, 14.6), r=4.0)
# ---------------- kickstand ----------------
LEGS = [(-21.3, 2.6), (36.4, 57.4)]
LEG_X = (53.4, X_DP + ARMOR)
LEG_FOOT = 24.0
TUBE_Y = (-21.6, 57.7)

def ysl(g, y): return slab(g, 'y', y[0], y[1])

def peg_list(T, fl, ids):
    out = []
    for b in ids:
        p = T[fl == b].reshape(-1, 3); lo, hi = p.min(0), p.max(0)
        out.append(((lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2, lo[0], hi[0], (hi[1] - lo[1]) / 2))
    return out

def load_template():
    T = load_stl(TEMPLATE)
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components
    v = np.round(T.reshape(-1, 3), 4); u, inv = np.unique(v, axis=0, return_inverse=True); inv = inv.reshape(-1, 3)
    r = np.r_[inv[:, 0], inv[:, 1]]; c = np.r_[inv[:, 1], inv[:, 2]]
    _, lab = connected_components(coo_matrix((np.ones(len(r)), (r, c)), shape=(len(u),) * 2), directed=False)
    fl = lab[inv[:, 0]]
    info = {}
    for b in np.unique(fl):
        p = T[fl == b].reshape(-1, 3); info[b] = (p.min(0), p.max(0), (fl == b).sum())
    sleeve = max(info, key=lambda b: info[b][2] if info[b][1][1] > 0 else -1)
    skin = [b for b in info if info[b][2] == 12 and info[b][0][0] > 30]
    base_pegs = [b for b in info if info[b][2] == 288 and info[b][0][0] > 30]
    lid_pegs = [b for b in info if info[b][2] == 288 and info[b][0][0] < 30]
    return T, fl, sleeve, skin, base_pegs, lid_pegs

def mesh_out(name, g, f):
    t = time.time(); V, F = surface_nets(g, f)
    vol = orient_check(V, F)
    if vol < 0: F = F[:, ::-1]
    save_stl(f'{OUT}/{name}.stl', V[F]); print(f'{name}: {len(F)} tris, vol {abs(vol)/1000:.1f} cm3, {time.time()-t:.1f}s', flush=True)

def ribs(g, axis_along, face_axis, face_val, inward, across_axis, positions, span, w=0.8, d=0.5):
    """parallel decorative grooves on a flat face. Returns sdf of the groove volume (to subtract)."""
    out = None
    for p in positions:
        lo = [0, 0, 0]; hi = [0, 0, 0]
        ia = 'xyz'.index(axis_along); ifc = 'xyz'.index(face_axis); ic = 'xyz'.index(across_axis)
        lo[ia], hi[ia] = span
        lo[ic], hi[ic] = p - w / 2, p + w / 2
        if inward > 0: lo[ifc], hi[ifc] = face_val - 1, face_val + d
        else: lo[ifc], hi[ifc] = face_val - d, face_val + 1
        b = box(g, lo, hi); out = b if out is None else U(out, b)
    return out

# =============== BASE ===============
def build_base(Tm, fl, sleeve, skin, base_pegs):
    g = Grid((10.4, Y0 - 0.6, -0.6), (K[0] + 4.6, Y1 + 0.6, A[1] + RJ + 0.6), H)
    t = time.time()
    f = mesh_sdf(g, Tm[fl == sleeve], 1.0)
    for b in skin: f = U(f, mesh_sdf(g, Tm[fl == b], 1.0))
    print('sleeve sdf', round(time.time() - t, 1), flush=True)
    # --- v3 sleeve rework ---
    f = D(f, box(g, (0, Y0 - 1, 0), (RAIL_CUT_X, Y1 + 1, 70)))                       # sliding rails / hooks
    f = D(f, box(g, (0, KB_OPEN['y'][0], -1), (18.9, KB_OPEN['y'][1], KB_OPEN['ztop'])))  # keyboard opening
    floor = box(g, (FLOOR['x'][0], -25.7, FLOOR['z'][0]), (FLOOR['x'][1], 62.0, FLOOR['z'][1]))
    floor = I(floor, cbox(g, (FLOOR['x'][0], -25.7, -5), (FLOOR['x'][1] + 5, 62.0, 10), 0.8))
    tri = None
    for i in range(FLOOR_TRI['n']):
        y0 = FLOOR_TRI['y0'] + i * FLOOR_TRI['step']; yb = y0 + FLOOR_TRI['base']; ym = y0 + FLOOR_TRI['base'] / 2
        xa, xb = FLOOR_TRI['x']
        pts = [(xa, y0), (xa, yb), (xb, ym)] if i % 2 == 0 else [(xb, y0), (xb, yb), (xa, ym)]
        t_ = poly2(g, 'z', pts); tri = t_ if tri is None else U(tri, t_)
    f = U(f, floor)
    f = D(f, I(tri, slab(g, 'z', -1, FLOOR['z'][1] + 0.3)))
    pegs = sorted(peg_list(Tm, fl, base_pegs))
    for i, (yc, zc, x0, x1, r) in enumerate(pegs):
        dy = PEG_SHIFT if i < 2 else (-PEG_SHIFT if i >= len(pegs) - 2 else 0.0)
        f = U(f, cyl(g, 'x', (yc + dy, zc), r, x0 - 0.3, x1))
    # --- bay frame: shallow box + full-depth end rails and bottom rail (chamfered) ---
    shallow = I(crect2(g, 'x', (Y0, BAY_Z[0]), (Y1, BAY_Z[1]), 2.0), slab(g, 'x', BACK - 0.75, X_SH))
    rails = U(box(g, (BACK, Y0, BAY_Z[0]), (X_DP, BAY_IN[0], BAY_Z[1])),
              box(g, (BACK, BAY_IN[1], BAY_Z[0]), (X_DP, Y1, BAY_Z[1])))
    rails = U(rails, box(g, (BACK, Y0, BAY_Z[0]), (X_DP, Y1, BAY_ZIN[0])))
    rails = I(rails, cbox(g, (BACK - 5, Y0, BAY_Z[0]), (X_DP, Y1, BAY_Z[1] + 5), 2.0))
    bay = U(shallow, rails)
    cav = box(g, (BACK, BAY_IN[0], BAY_ZIN[0]), (X_DP + 1, BAY_IN[1], BAY_ZIN[1]))
    bay = D(bay, cav)
    # cover support ledge (shallow plane)
    ledge = D(box(g, (X_SH - COV_T - 0.8, BAY_IN[0], BAY_ZIN[0]), (X_SH - COV_T, BAY_IN[1], BAY_ZIN[1])),
              box(g, (X_SH - 5, BAY_IN[0] + 0.8, BAY_ZIN[0] + 0.8), (X_SH, BAY_IN[1] - 0.8, BAY_ZIN[1] - 0.8)))
    bay = U(bay, ledge)
    # PaHub hump: top wall stops at the cover plane; the cover's hump continues it
    bay = D(bay, box(g, (X_SH - COV_T, HUMP_Y[0], BAY_ZIN[1] - 1), (X_DP + 2, HUMP_Y[1], 60)))
    # cover screw bosses (long columns down the trough corners)
    for (yc, zc) in SCREWS:
        bay = U(bay, cyl(g, 'x', (yc, zc), 3.0, BACK - 0.5, X_DP - COV_T))
        bay = D(bay, cyl(g, 'x', (yc, zc), PILOT_M3, X_DP - COV_T - 9, X_DP))
    f = U(f, bay)
    # --- ears (pivot A) + kickstand lugs (pivot K), both ends, one chamfered block ---
    for ez in (EAR0, EAR1):
        ear = U(circle2(g, 'y', A, RJ), rect2(g, 'y', (BACK - 0.75, BAY_ZIN[1]), (X_SH, A[1])))
        lug = U(circle2(g, 'y', K, RK + 0.5), rect2(g, 'y', (X_SH, BAY_ZIN[1] - 2), (K[0] + RK + 0.5, K[1])))
        blk = I(U(ear, lug), ysl(g, ez))
        blk = I(blk, cbox(g, (0, Y0, 0), (K[0] + RK + 0.5, Y1, A[1] + RJ), 1.2))
        f = U(f, blk)
    f = D(f, cyl(g, 'y', A, CLR_M3, Y0 - 1, Y1 + 1))
    f = D(f, cyl(g, 'y', K, CLR_M3, Y0 - 1, Y1 + 1))
    # screw-head counterbores on the outer faces of ears / lugs
    for yy in ((Y0 - 1, Y0 + 1.2), (Y1 - 1.2, Y1 + 1)):
        f = D(f, cyl(g, 'y', A, 3.0, *yy)); f = D(f, cyl(g, 'y', K, 3.0, *yy))
    # cable windows bay -> joint
    for w in BAY_WIN:
        f = D(f, box(g, (BACK + 0.45, w[0], BAY_ZIN[1] - 0.5), (X_SH - COV_T + 0.1, w[1], BAY_Z[1] + 1)))
    # Cardputer Grove cable entry notches (both ends, aligned with the sleeve side openings)
    for yy in ((Y0 - 1, BAY_IN[0] + 0.5), (BAY_IN[1] - 0.5, Y1 + 1)):
        f = D(f, box(g, (SIDE_NOTCH['x'][0], yy[0], SIDE_NOTCH['z'][0]), (SIDE_NOTCH['x'][1], yy[1], SIDE_NOTCH['z'][1])))
    # groove for the cover's top tongue
    f = D(f, box(g, (X_SH - COV_T, BAY_IN[0], BAY_ZIN[1] - 0.2), (X_SH - 0.7, BAY_IN[1], BAY_ZIN[1] + 1.0)))
    # styling: ribbed end faces
    pos = list(np.arange(40.0, 55.5, 2.2))
    f = D(f, ribs(g, 'z', 'y', Y0, +1, 'x', pos, (4.0, 36.0)))
    f = D(f, ribs(g, 'z', 'y', Y1, -1, 'x', pos, (4.0, 36.0)))
    return g, f

# =============== JOINT ===============
def joint_sdf(g):
    cap = capsule2(g, 'y', B, A, RJ)
    arms = I(I(cap, U(ysl(g, ARM0), ysl(g, ARM1))), D(slab(g, 'x', -99, A[0] - 6.2), circle2(g, 'y', A, 6.2)))
    mid = I(cap, ysl(g, JMID))
    mid = D(mid, U(circle2(g, 'y', B, RL + 0.4), rect2(g, 'y', (-5, 50), (LID_X[1] + 0.4, B[1]))))
    f = U(arms, mid)
    f = D(f, I(capsule2(g, 'y', B, A, RJ - 1.5), ysl(g, (CABLE_Y[0] + 0.5, CABLE_Y[1] - 0.5))))
    f = D(f, I(circle2(g, 'y', B, CLR_M3), slab(g, 'y', Y0 - 1, Y1 + 1)))
    f = D(f, I(circle2(g, 'y', A, PILOT_M3), slab(g, 'y', Y0 - 1, Y1 + 1)))
    for yy in ((Y0 - 1, Y0 + 1.2), (Y1 - 1.2, Y1 + 1)):          # head counterbores on the arms
        f = D(f, cyl(g, 'y', B, 3.0, *yy))
    for yy in ((-20.0, -14.0), (50.0, 56.0)):                       # Cardputer retaining tabs
        f = U(f, box(g, (20.0, yy[0], 55.9), (33.5, yy[1], B[1] - RJ + 0.5)))
    f = D(f, box(g, (21.0, -12.0, 57.0), (33.0, 48.0, 61.0)))       # cable slot to the Cardputer top edge
    for w in BAY_WIN:
        f = D(f, box(g, (BACK + 0.05, w[0], 57.0), (X_SH - COV_T + 0.1, w[1], 61.6)))
    # styling: ribs along the top, angular cut-backs on the arm ends
    top = B[1] + RJ
    f = D(f, ribs(g, 'y', 'z', top, -1, 'x', [14.0, 16.4, 18.8, 27.7, 30.1, 32.5], (-15.0, 51.0), 1.0, 0.6))
    for (ya, yb) in ((Y0 - 1, Y0 + 0.8), (Y1 - 0.8, Y1 + 1)):
        f = D(f, I(poly2(g, 'y', [(14, top + 1), (30, top + 1), (26, top - 2.0), (18, top - 2.0)]), slab(g, 'y', ya, yb)))
    return f

def build_joint():
    g = Grid((B[0] - RJ - 0.5, Y0 - 0.6, 55.0), (A[0] + RJ + 0.5, Y1 + 0.6, A[1] + RJ + 0.5), H)
    return g, joint_sdf(g)

# =============== LID ===============
def build_lid(Tm, fl, lid_pegs):
    g = Grid((LID_X[0] - 0.6, Y0 - 0.6, -0.6), (LID_X[1] + 0.6, Y1 + 0.6, B[1] + RL + 0.6), H)
    body = I(cbox(g, (LID_X[0], Y0, 0.0), (LID_X[1], Y1, LID_ZTOP + 10), 2.0), slab(g, 'z', -9, LID_ZTOP))
    body = U(I(body, slab(g, 'z', -9, LID_END_TOP)), I(body, ysl(g, LCAP)))
    body = U(body, I(circle2(g, 'y', B, RL), ysl(g, LCAP)))
    cav = box(g, (LID_X[0] + COV_T, Y0 + 1.8, 1.6), (LID_PLATE, Y1 - 1.8, LID_END_TOP - 1.6))
    cav = U(cav, I(U(rect2(g, 'y', (LID_X[0] + COV_T, 1.6), (LID_PLATE, B[1])), circle2(g, 'y', B, RL - 1.6)), ysl(g, CABLE_Y)))
    f = D(body, cav)
    f = D(f, I(rect2(g, 'y', (B[0], B[1] - 1.0), (B[0] + 9, B[1] + 9)), ysl(g, CABLE_Y)))
    f = D(f, box(g, (LID_X[0] - 1, Y0 + 1.8, 1.6), (LID_X[0] + COV_T, Y1 - 1.8, 57.5)))
    ledge = D(box(g, (LID_X[0] + COV_T, Y0 + 1.8, 1.6), (LID_X[0] + COV_T + 0.8, Y1 - 1.8, 57.5)),
              box(g, (-5, Y0 + 2.6, 2.4), (5, Y1 - 2.6, 56.7)))
    f = U(f, ledge)
    f = D(f, box(g, (LID_X[0] + 0.75, CABLE_Y[0], 57.3), (LID_X[0] + COV_T + 0.05, CABLE_Y[1], 59.2)))
    for (yc, zc, x0, x1, r) in peg_list(Tm, fl, lid_pegs):
        h_ = x1 - 20.03
        f = U(f, cyl(g, 'x', (yc + TY, TZ - zc), r, LID_PLATE - h_, LID_PLATE + 0.5))
    for (yc, zc) in SCREWS:
        f = U(f, cyl(g, 'x', (yc, zc), 3.0, LID_X[0] + COV_T, LID_PLATE + 0.5))
        f = D(f, cyl(g, 'x', (yc, zc), PILOT_M3, LID_X[0], LID_PLATE - 1.5))
    f = D(f, I(circle2(g, 'y', B, PILOT_M3), slab(g, 'y', Y0 - 1, Y1 + 1)))
    # styling: ribbed end faces + chamfer notches on the free edge
    pos = [1.6, 3.8, 6.0, 8.2]
    f = D(f, ribs(g, 'z', 'y', Y0, +1, 'x', pos, (8.0, 50.0)))
    f = D(f, ribs(g, 'z', 'y', Y1, -1, 'x', pos, (8.0, 50.0)))
    for yc in (-8.0, 44.0):
        f = D(f, I(poly2(g, 'y', [(-2, -1), (12, -1), (12, 0.8), (-2, 0.8)]), ysl(g, (yc - 6, yc + 6))))
        f = D(f, I(poly2(g, 'x', [(yc - 7, -1), (yc + 7, -1), (yc + 5, 0.8), (yc - 5, 0.8)]), slab(g, 'x', -3, 13)))
    return g, f

# =============== COVERS ===============
def armor_lid(g):
    """raised cyberpunk plate on the lid cover, coords (y, z) on the x-normal plane"""
    P = poly2(g, 'x', [(-22.5, 9.0), (-16.5, 3.4), (58.6, 3.4), (58.6, 46.5), (49.6, 55.6), (-22.5, 55.6)])
    cuts = [poly2(g, 'x', [(-17.5, 50.5), (-11.5, 50.5), (-1.5, 30.5), (-7.5, 30.5)]),
            poly2(g, 'x', [(-6.0, 50.5), (-2.5, 50.5), (7.5, 30.5), (4.0, 30.5)]),
            poly2(g, 'x', [(44.5, 50.0), (53.0, 50.0), (53.0, 41.5)]),
            poly2(g, 'x', [(-22.6, 19.0), (-18.6, 23.0), (-18.6, 33.0), (-22.6, 37.0)]),
            seg2(g, 'x', (18.0, 55.7), (26.0, 47.5), 0.9), seg2(g, 'x', (26.0, 47.5), (58.7, 47.5), 0.9),
            seg2(g, 'x', (-22.6, 14.0), (8.0, 14.0), 0.9),
            rect2(g, 'x', (IR_PILL['y'][0] - 1.5, IR_PILL['z'][0] - 1.5), (IR_PILL['y'][1] + 1.5, IR_PILL['z'][1] + 1.5), IR_PILL['r'] + 1.5)]
    for c in cuts: P = D(P, c)
    for (yc, zc) in SCREWS: P = D(P, circle2(g, 'x', (yc, zc), 3.2))
    return P

def build_lid_cover():
    g = Grid((LID_X[0] - ARMOR - 0.6, Y0, 1.0), (LID_X[0] + COV_T + 0.6, Y1, 60.0), H)
    x0 = LID_X[0]
    plate = box(g, (x0, Y0 + 2.0, 1.8), (x0 + COV_T, Y1 - 2.0, 57.3))
    tongue = box(g, (x0 + 0.85, CABLE_Y[0] + 0.3, 57.0), (x0 + COV_T - 0.05, CABLE_Y[1] - 0.3, 59.0))
    f = U(plate, tongue)
    f = U(f, extrude(armor_lid(g), slab(g, 'x', x0 - ARMOR, x0 + 0.1)))
    f = D(f, rect2(g, 'x', (IR_PILL['y'][0], IR_PILL['z'][0]), (IR_PILL['y'][1], IR_PILL['z'][1]), IR_PILL['r']))
    for (yc, zc) in SCREWS:
        f = D(f, cyl(g, 'x', (yc, zc), CLR_M3, -5, 5))
    return g, f

def armor_base(g):
    hump = poly2(g, 'x', [(4.8, 3.4), (34.2, 3.4), (34.2, 47.5), (27.8, 53.9), (4.8, 53.9)])
    trough = poly2(g, 'x', [(-22.6, 3.4), (58.8, 3.4), (58.8, 15.5), (54.3, 20.4), (-18.1, 20.4), (-22.6, 15.9)])
    P = U(hump, trough)
    cuts = [poly2(g, 'x', [(8.5, 47.0), (13.5, 47.0), (21.5, 31.0), (16.5, 31.0)]),
            poly2(g, 'x', [(24.0, 51.5), (30.5, 51.5), (30.5, 45.0)]),
            seg2(g, 'x', (8.0, 26.0), (30.5, 26.0), 0.9),
            rect2(g, 'x', (-14.0, 7.5), (0.0, 16.0), 3.0),          # grey "pill" insert pocket
            rect2(g, 'x', (39.0, 7.5), (53.0, 16.0), 3.0)]
    for z in (9.0, 11.5, 14.0):
        cuts.append(seg2(g, 'x', (12.0, z), (27.0, z), 0.9))
    for c in cuts: P = D(P, c)
    for (yc, zc) in SCREWS: P = D(P, circle2(g, 'x', (yc, zc), 3.2))
    return P

def build_base_cover():
    g = Grid((X_SH - COV_T - 0.6, Y0, -0.6), (X_DP + ARMOR + 0.6, Y1, BAY_Z[1] + 0.6), H)
    yi0, yi1 = BAY_IN[0] + 0.2, BAY_IN[1] - 0.2
    # shallow plates over the NFC / RFID pockets (+ top tongue)
    plate = box(g, (X_SH - COV_T, yi0, TROUGH_Z - 2), (X_SH, yi1, BAY_ZIN[1] - 0.2))
    tongue = box(g, (X_SH - COV_T + 0.1, yi0 + 0.4, BAY_ZIN[1] - 0.5), (X_SH - 0.8, yi1 - 0.4, BAY_ZIN[1] + 0.85))
    tongue = D(tongue, box(g, (40, HUMP_Y[0] - 0.5, 0), (60, HUMP_Y[1] + 0.5, 60)))
    # deep shell = PaHub hump  U  cable trough (an inverted T)
    outer = U(box(g, (X_SH - COV_T, HUMP_Y[0] + 0.15, BAY_ZIN[0] + 0.2), (X_DP, HUMP_Y[1] - 0.15, BAY_Z[1])),
              box(g, (X_SH - COV_T, yi0, BAY_ZIN[0] + 0.2), (X_DP, yi1, TROUGH_Z)))
    outer = I(outer, cbox(g, (30, yi0, BAY_ZIN[0] + 0.2 - 5), (X_DP, yi1, BAY_Z[1]), 2.0))
    inner = U(box(g, (30, HUMP_Y[0] + 1.75, BAY_ZIN[0] + 0.2 - 1), (X_DP - COV_T, HUMP_Y[1] - 1.75, BAY_ZIN[1] - 0.2)),
              box(g, (30, yi0 - 1, BAY_ZIN[0] + 0.2 - 1), (X_DP - COV_T, yi1 + 1, TROUGH_Z - COV_T)))
    shell = D(outer, inner)
    f = U(U(plate, tongue), shell)
    # chamfer the hump's top-outer edge
    zc = BAY_Z[1] - 2.5
    f = I(f, poly2(g, 'y', [(0, -9), (99, -9), (99, zc - (99 - X_DP)), (X_DP, zc), (X_DP - 2.5, BAY_Z[1]), (0, BAY_Z[1] + 5)]))
    f = U(f, extrude(armor_base(g), slab(g, 'x', X_DP - 0.1, X_DP + ARMOR)))
    for (yc, zc) in SCREWS:
        f = D(f, cyl(g, 'x', (yc, zc), CLR_M3, 40, 70))
    return g, f

# =============== KICKSTAND ===============
def build_kickstand():
    g = Grid((K[0] - RK - 2.5, TUBE_Y[0] - 0.6, LEG_FOOT - 0.6), (LEG_X[1] + 0.6, TUBE_Y[1] + 0.6, K[1] + RK + 0.6), H)
    f = I(circle2(g, 'y', K, RK), ysl(g, TUBE_Y))
    L = K[1] - LEG_FOOT
    for (ya, yb) in LEGS:
        W = yb - ya
        leg = I(crect2(g, 'x', (ya, LEG_FOOT), (yb, K[1]), 3.0), slab(g, 'x', *LEG_X))
        # X-truss windows
        win = rect2(g, 'x', (ya + 3.0, LEG_FOOT + 3.0), (yb - 3.0, K[1] - RK - 2.0), 0.5)
        zt = K[1] - RK - 2.0; zm = (LEG_FOOT + 3.0 + zt) / 2
        mem = U(seg2(g, 'x', (ya + 3.0, LEG_FOOT + 3.0), (yb - 3.0, zm), 3.2), seg2(g, 'x', (yb - 3.0, zm), (ya + 3.0, zt), 3.2))
        leg = D(leg, D(win, mem))
        # foot: angled grip ridge
        f = U(f, leg)
    f = D(f, cyl(g, 'y', K, PILOT_M3, TUBE_Y[0] - 1, TUBE_Y[1] + 1))
    return g, f

if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    parts = sys.argv[1:] or ['joint', 'lid', 'base_cover', 'lid_cover', 'kickstand', 'base']
    Tm, fl, sleeve, skin, base_pegs, lid_pegs = load_template()
    for p in parts:
        if p == 'base': g, f = build_base(Tm, fl, sleeve, skin, base_pegs)
        elif p == 'joint': g, f = build_joint()
        elif p == 'lid': g, f = build_lid(Tm, fl, lid_pegs)
        elif p == 'base_cover': g, f = build_base_cover()
        elif p == 'lid_cover': g, f = build_lid_cover()
        elif p == 'kickstand': g, f = build_kickstand()
        np.save(f'{OUT}/{p}_sdf.npy', f); np.save(f'{OUT}/{p}_grid.npy', np.r_[g.o, g.n, g.h])
        mesh_out(p, g, f)
