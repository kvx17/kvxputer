"""Cardputer ADV hinged module case - parametric build.
All coordinates are the ASSEMBLED/CLOSED position, in the frame of the user's slip-in sleeve STL:
  x : -x = Cardputer face (lid side), +x = back (bottom module bay)
  y : length of the Cardputer (87.7 mm sleeve)
  z : up; Cardputer slides in from +z, hinge line runs along y at the top.
Parts: base, joint, lid, base_cover, lid_cover.
"""
import sys, os, time, numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from sdf import *

H = float(os.environ.get('RES', 0.2))
TEMPLATE = os.environ.get('TEMPLATE', '/home/claude/in/case.stl')
OUT = os.environ.get('OUTDIR', '/home/claude/out')

# ---------------- parameters ----------------
Y0, Y1 = -26.4, 62.5              # overall length of all parts
EZ, GAP = 4.5, 0.3                # hinge end-zone width, running clearance
B = (4.9, 64.0)                   # lid <-> joint pivot (x, z)
A = (41.6, 64.0)                  # joint <-> base pivot (x, z)
RJ = 5.5                          # joint / knuckle radius
RL = 5.6                          # lid cap radius (half lid thickness)
CLR_M3, PILOT_M3 = 1.7, 1.3       # radii: M3 clearance, M3 self-tap pilot
# end zones (arms / ears) and middle zones
ARM0, ARM1 = (Y0, -21.6), (57.7, Y1)            # joint arms (at B)
EAR0, EAR1 = (Y0, -21.9), (58.0, Y1)            # base ears (at A)
JMID = (-21.6, 57.7)                             # joint middle body
LCAP = (-21.3, 57.4)                             # lid cap (round top)
CABLE_Y = (-17.8, 53.9)                          # open cable zone through the hinge
# base bay
BACK = 37.75                      # outer face of the sleeve back wall
BAY_X1 = 47.1                     # shallow bay outer face (NFC / RFID side)
COV_T = 1.6                       # cover thickness
BAY_IN = (-24.6, 60.7)            # bay inner y
BAY_Z = (0.0, 57.8); BAY_ZIN = (1.8, 56.0)
HUMP_Y = (3.2, 35.8); HUMP_X1 = 56.6   # PaHub bulge (16.3 mm + clearance from BACK)
SCREWS = [(-21.6, 4.6), (57.7, 4.6)]   # (y, z) of cover screws, both halves
BAY_WIN = [(1.2, 8.6), (30.2, 36.6)]   # cable windows bay -> joint (y ranges)
# lid
LID_X = (-0.7, 10.5)              # outer face .. face touching the Cardputer side
LID_PLATE = 8.9                   # module mounting face (modules hang towards -x)
LID_ZTOP = 64.0
LID_END_TOP = 58.0                # lid height under the joint arms
TY = 118.35; TZ = 60.0            # template lid -> closed: y' = y+TY, z' = TZ - z, x' mirrored
IR_SLOT = dict(yc=-89.95 + TY, zc=10.1, hy=9.5, hz=4.5)

def peg_list(T, fl, ids):
    out = []
    for b in ids:
        p = T[fl == b].reshape(-1, 3); lo, hi = p.min(0), p.max(0)
        out.append(((lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2, lo[0], hi[0], (hi[1] - lo[1]) / 2))
    return out

def load_template():
    T = load_stl(TEMPLATE)
    # connected components
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components
    v = np.round(T.reshape(-1, 3), 4); u, inv = np.unique(v, axis=0, return_inverse=True); inv = inv.reshape(-1, 3)
    r = np.r_[inv[:, 0], inv[:, 1]]; c = np.r_[inv[:, 1], inv[:, 2]]
    _, lab = connected_components(coo_matrix((np.ones(len(r)), (r, c)), shape=(len(u),) * 2), directed=False)
    fl = lab[inv[:, 0]]
    # identify bodies by bbox
    info = {}
    for b in np.unique(fl):
        p = T[fl == b].reshape(-1, 3); info[b] = (p.min(0), p.max(0), (fl == b).sum())
    sleeve = max(info, key=lambda b: info[b][2] if info[b][1][1] > 0 else -1)
    skin = [b for b in info if info[b][2] == 12 and info[b][0][0] > 30]
    base_pegs = [b for b in info if info[b][2] == 288 and info[b][0][0] > 30]
    lid_pegs = [b for b in info if info[b][2] == 288 and info[b][0][0] < 30]
    return T, fl, sleeve, skin, base_pegs, lid_pegs

def ysl(g, y): return slab(g, 'y', y[0], y[1])

def mesh_out(name, g, f):
    t = time.time(); V, F = surface_nets(g, f)
    vol = orient_check(V, F)
    if vol < 0: F = F[:, ::-1]
    save_stl(f'{OUT}/{name}.stl', V[F]); print(f'{name}: {len(F)} tris, vol {abs(vol)/1000:.1f} cm3, {time.time()-t:.1f}s', flush=True)
    return V, F

# ---------------- parts ----------------
def build_base(Tm, fl, sleeve, skin, base_pegs):
    g = Grid((10.4, Y0 - 0.6, -0.6), (BAY_X1 + 0.6, Y1 + 0.6, A[1] + RJ + 0.6), H)
    t = time.time()
    f = mesh_sdf(g, Tm[fl == sleeve], 1.0)
    for b in skin: f = U(f, mesh_sdf(g, Tm[fl == b], 1.0))
    print('sleeve sdf', round(time.time() - t, 1), flush=True)
    for (yc, zc, x0, x1, r) in peg_list(Tm, fl, base_pegs):
        f = U(f, cyl(g, 'x', (yc, zc), r, x0 - 0.3, x1))
    # bay shell
    bay = I(rect2(g, 'x', (Y0, BAY_Z[0]), (Y1, BAY_Z[1]), 2.0), slab(g, 'x', BACK - 0.75, BAY_X1))
    cav = box(g, (BACK, BAY_IN[0], BAY_ZIN[0]), (BAY_X1 + 1, BAY_IN[1], BAY_ZIN[1]))
    bay = D(bay, cav)
    # cover support ledge
    ledge = D(box(g, (BAY_X1 - COV_T - 0.8, BAY_IN[0], BAY_ZIN[0]), (BAY_X1 - COV_T, BAY_IN[1], BAY_ZIN[1])),
              box(g, (BAY_X1 - 5, BAY_IN[0] + 0.8, BAY_ZIN[0] + 0.8), (BAY_X1, BAY_IN[1] - 0.8, BAY_ZIN[1] - 0.8)))
    bay = U(bay, ledge)
    # PaHub bulge: base walls stop at the cover plane, the cover's hump carries on
    bay = D(bay, box(g, (BAY_X1 - COV_T, HUMP_Y[0], -2), (BAY_X1 + 2, HUMP_Y[1], 60)))
    # cover-screw bosses
    for (yc, zc) in SCREWS:
        bay = U(bay, cyl(g, 'x', (yc, zc), 3.0, BACK - 0.5, BAY_X1 - COV_T))
        bay = D(bay, cyl(g, 'x', (yc, zc), PILOT_M3, BACK + 1.5, BAY_X1))
    f = U(f, bay)
    # ears (pivot A) on both ends
    for ez in (EAR0, EAR1):
        ear = U(circle2(g, 'y', A, RJ), rect2(g, 'y', (BACK - 0.75, BAY_ZIN[1]), (A[0] + RJ, A[1])))
        ear = I(I(ear, ysl(g, ez)), slab(g, 'x', -99, BAY_X1))
        f = U(f, ear)
    f = D(f, cyl(g, 'y', A, CLR_M3, Y0 - 1, Y1 + 1))
    # cable windows bay -> joint
    for w in BAY_WIN:
        f = D(f, box(g, (BACK + 0.45, w[0], BAY_ZIN[1] - 0.5), (BAY_X1 - COV_T + 0.1, w[1], BAY_Z[1] + 1)))
    # groove for the cover's top tongue (cut last so ears don't fill it)
    f = D(f, box(g, (BAY_X1 - COV_T, BAY_IN[0], BAY_ZIN[1] - 0.2), (BAY_X1 - 0.7, BAY_IN[1], BAY_ZIN[1] + 1.0)))
    return g, f

def joint_sdf(g):
    cap = capsule2(g, 'y', B, A, RJ)
    arms = I(I(cap, U(ysl(g, ARM0), ysl(g, ARM1))), D(slab(g, 'x', -99, A[0] - 6.2), circle2(g, 'y', A, 6.2)))
    mid = I(cap, ysl(g, JMID))
    mid = D(mid, U(circle2(g, 'y', B, RL + 0.4), rect2(g, 'y', (-5, 50), (LID_X[1] + 0.4, B[1]))))
    f = U(arms, mid)
    # hollow cable channel
    f = D(f, I(capsule2(g, 'y', B, A, RJ - 1.5), ysl(g, (CABLE_Y[0] + 0.5, CABLE_Y[1] - 0.5))))
    # pins: clearance through the arms (B), pilots in the middle bosses (A)
    f = D(f, I(circle2(g, 'y', B, CLR_M3), slab(g, 'y', Y0 - 1, Y1 + 1)))
    f = D(f, I(circle2(g, 'y', A, PILOT_M3), slab(g, 'y', Y0 - 1, Y1 + 1)))
    # tabs that drop into the slot and stop the Cardputer sliding out
    for yy in ((-20.0, -14.0), (50.0, 56.0)):
        f = U(f, box(g, (20.0, yy[0], 55.9), (33.5, yy[1], B[1] - RJ + 0.5)))
    # cable openings: down to the Cardputer's top edge, and down into the bay
    f = D(f, box(g, (21.0, -12.0, 57.0), (33.0, 48.0, 61.0)))
    for w in BAY_WIN:
        f = D(f, box(g, (BACK + 0.05, w[0], 57.0), (BAY_X1 - COV_T + 0.1, w[1], 61.6)))
    return f

def build_joint():
    g = Grid((B[0] - RJ - 0.5, Y0 - 0.6, 55.0), (A[0] + RJ + 0.5, Y1 + 0.6, A[1] + RJ + 0.5), H)
    return g, joint_sdf(g)

def build_lid(Tm, fl, lid_pegs):
    g = Grid((LID_X[0] - 0.6, Y0 - 0.6, -0.6), (LID_X[1] + 0.6, Y1 + 0.6, B[1] + RL + 0.6), H)
    body = I(rbox(g, (LID_X[0], Y0, 0.0), (LID_X[1], Y1, LID_ZTOP + 10), 1.5), slab(g, 'z', -9, LID_ZTOP))
    body = U(I(body, slab(g, 'z', -9, LID_END_TOP)), I(body, ysl(g, LCAP)))
    body = U(body, I(circle2(g, 'y', B, RL), ysl(g, LCAP)))
    # interior
    cav = box(g, (LID_X[0] + COV_T, Y0 + 1.8, 1.6), (LID_PLATE, Y1 - 1.8, LID_END_TOP - 1.6))
    cav = U(cav, I(U(rect2(g, 'y', (LID_X[0] + COV_T, 1.6), (LID_PLATE, B[1])), circle2(g, 'y', B, RL - 1.6)), ysl(g, CABLE_Y)))
    f = D(body, cav)
    # open the cap towards the joint (cables pass the pivot line here)
    f = D(f, I(rect2(g, 'y', (B[0], B[1] - 1.0), (B[0] + 9, B[1] + 9)), ysl(g, CABLE_Y)))
    # cover window, ledge, top groove
    f = D(f, box(g, (LID_X[0] - 1, Y0 + 1.8, 1.6), (LID_X[0] + COV_T, Y1 - 1.8, 57.5)))
    ledge = D(box(g, (LID_X[0] + COV_T, Y0 + 1.8, 1.6), (LID_X[0] + COV_T + 0.8, Y1 - 1.8, 57.5)),
              box(g, (-5, Y0 + 2.6, 2.4), (5, Y1 - 2.6, 56.7)))
    f = U(f, ledge)
    f = D(f, box(g, (LID_X[0] + 0.75, CABLE_Y[0], 57.3), (LID_X[0] + COV_T + 0.05, CABLE_Y[1], 59.2)))
    # module pegs (from the user's template, mirrored onto the lid)
    for (yc, zc, x0, x1, r) in peg_list(Tm, fl, lid_pegs):
        h_ = x1 - 20.03
        f = U(f, cyl(g, 'x', (yc + TY, TZ - zc), r, LID_PLATE - h_, LID_PLATE + 0.5))
    # cover screw bosses
    for (yc, zc) in SCREWS:
        f = U(f, cyl(g, 'x', (yc, zc), 3.0, LID_X[0] + COV_T, LID_PLATE + 0.5))
        f = D(f, cyl(g, 'x', (yc, zc), PILOT_M3, LID_X[0], LID_PLATE - 1.5))
    # pivot B pilots (screws come through the joint arms)
    f = D(f, I(circle2(g, 'y', B, PILOT_M3), slab(g, 'y', Y0 - 1, Y1 + 1)))
    return g, f

def build_base_cover():
    g = Grid((BAY_X1 - COV_T - 0.6, Y0, -0.6), (HUMP_X1 + 0.6, Y1, BAY_Z[1] + 0.6), H)
    plate = box(g, (BAY_X1 - COV_T, BAY_IN[0] + 0.2, BAY_ZIN[0] + 0.2), (BAY_X1, BAY_IN[1] - 0.2, BAY_ZIN[1] - 0.2))
    tongue = box(g, (BAY_X1 - COV_T + 0.1, BAY_IN[0] + 0.6, BAY_ZIN[1] - 0.5), (BAY_X1 - 0.8, BAY_IN[1] - 0.6, BAY_ZIN[1] + 0.85))
    tongue = D(tongue, box(g, (40, HUMP_Y[0] - 0.5, 0), (60, HUMP_Y[1] + 0.5, 60)))
    hump = I(rbox(g, (BACK, HUMP_Y[0] + 0.15, BAY_Z[0]), (HUMP_X1, HUMP_Y[1] - 0.15, BAY_Z[1]), 3.0), slab(g, 'x', BAY_X1 - COV_T, 99))
    f = U(U(plate, tongue), hump)
    f = D(f, rbox(g, (BACK, HUMP_Y[0] + 1.75, BAY_ZIN[0] + 0.2), (HUMP_X1 - COV_T, HUMP_Y[1] - 1.75, BAY_ZIN[1] - 0.2), 1.5))
    for (yc, zc) in SCREWS:
        f = D(f, cyl(g, 'x', (yc, zc), CLR_M3, 40, 60))
        f = D(f, cyl(g, 'x', (yc, zc), 3.0, BAY_X1 - 0.6, 60))   # head recess
    return g, f

def build_lid_cover():
    g = Grid((LID_X[0] - 0.6, Y0, 1.0), (LID_X[0] + COV_T + 0.6, Y1, 60.0), H)
    x0 = LID_X[0]
    plate = box(g, (x0, Y0 + 2.0, 1.8), (x0 + COV_T, Y1 - 2.0, 57.3))
    tongue = box(g, (x0 + 0.85, CABLE_Y[0] + 0.3, 57.0), (x0 + COV_T - 0.05, CABLE_Y[1] - 0.3, 59.0))
    f = U(plate, tongue)
    s = IR_SLOT
    f = D(f, rect2(g, 'x', (s['yc'] - s['hy'], s['zc'] - s['hz']), (s['yc'] + s['hy'], s['zc'] + s['hz']), 2.0))
    for (yc, zc) in SCREWS:
        f = D(f, cyl(g, 'x', (yc, zc), CLR_M3, -5, 5))
        f = D(f, cyl(g, 'x', (yc, zc), 3.0, -5, x0 + 0.6))
    return g, f

if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    parts = sys.argv[1:] or ['joint', 'lid', 'base_cover', 'lid_cover', 'base']
    Tm, fl, sleeve, skin, base_pegs, lid_pegs = load_template()
    for p in parts:
        t = time.time()
        if p == 'base': g, f = build_base(Tm, fl, sleeve, skin, base_pegs)
        elif p == 'joint': g, f = build_joint()
        elif p == 'lid': g, f = build_lid(Tm, fl, lid_pegs)
        elif p == 'base_cover': g, f = build_base_cover()
        elif p == 'lid_cover': g, f = build_lid_cover()
        np.save(f'{OUT}/{p}_sdf.npy', f); np.save(f'{OUT}/{p}_grid.npy', np.r_[g.o, g.n, g.h])
        mesh_out(p, g, f)
