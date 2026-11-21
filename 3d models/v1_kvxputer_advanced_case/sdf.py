"""Minimal SDF modelling kernel: primitives, CSG, mesh->SDF, surface nets, STL IO.
Convention: negative = inside. Units: mm."""
import numpy as np, struct

# ---------------- grid ----------------
class Grid:
    def __init__(self, lo, hi, h=0.2, jitter=(0.0123, 0.0071, 0.0037)):
        self.h = h
        self.o = np.array(lo, float) - h * 3 + np.array(jitter)
        self.n = np.ceil((np.array(hi, float) + h * 3 - self.o) / h).astype(int) + 1
        nx, ny, nz = self.n
        self.X = (self.o[0] + h * np.arange(nx, dtype=np.float32)).reshape(-1, 1, 1)
        self.Y = (self.o[1] + h * np.arange(ny, dtype=np.float32)).reshape(1, -1, 1)
        self.Z = (self.o[2] + h * np.arange(nz, dtype=np.float32)).reshape(1, 1, -1)

    def full(self, v):
        return np.broadcast_to(np.float32(v), tuple(self.n)).copy()

# ---------------- CSG ----------------
U = np.minimum                      # union
I = np.maximum                      # intersection
def D(a, b): return np.maximum(a, -b)  # difference a-b

# ---------------- primitives ----------------
def box(g, lo, hi):
    lo = np.array(lo, np.float32); hi = np.array(hi, np.float32)
    c = (lo + hi) / 2; e = (hi - lo) / 2
    qx = np.abs(g.X - c[0]) - e[0]; qy = np.abs(g.Y - c[1]) - e[1]; qz = np.abs(g.Z - c[2]) - e[2]
    out = np.sqrt(np.maximum(qx, 0) ** 2 + np.maximum(qy, 0) ** 2 + np.maximum(qz, 0) ** 2)
    ins = np.minimum(np.maximum(np.maximum(qx, qy), qz), 0)
    return (out + ins).astype(np.float32)

def rbox(g, lo, hi, r):
    lo = np.array(lo, float) + r; hi = np.array(hi, float) - r
    return box(g, lo, hi) - np.float32(r)

def _axes(axis):
    return {'x': (1, 2, 0), 'y': (0, 2, 1), 'z': (0, 1, 2)}[axis]

def _coords(g, axis):
    C = (g.X, g.Y, g.Z); a, b, c = _axes(axis); return C[a], C[b], C[c]

def slab(g, axis, t0, t1):
    _, _, t = _coords(g, axis); return np.maximum(t0 - t, t - t1).astype(np.float32)

def cyl(g, axis, center2, r, t0, t1):
    """cylinder along axis; center2 = coords in the two other axes (x,z for y-axis, y,z for x-axis, x,y for z-axis)"""
    u, v, _ = _coords(g, axis)
    d = np.sqrt((u - center2[0]) ** 2 + (v - center2[1]) ** 2) - r
    return extrude(d, slab(g, axis, t0, t1))

def extrude(d2, s):
    """exact extrusion of 2D sdf d2 by slab sdf s"""
    w0 = np.maximum(d2, 0); w1 = np.maximum(s, 0)
    return (np.minimum(np.maximum(d2, s), 0) + np.sqrt(w0 ** 2 + w1 ** 2)).astype(np.float32)

def capsule2(g, axis, P, Q, r):
    """2D capsule (stadium) in the plane normal to axis, infinite along axis"""
    u, v, _ = _coords(g, axis)
    P = np.array(P, np.float32); Q = np.array(Q, np.float32); d = Q - P
    t = np.clip(((u - P[0]) * d[0] + (v - P[1]) * d[1]) / (d @ d), 0, 1)
    return (np.sqrt((u - P[0] - t * d[0]) ** 2 + (v - P[1] - t * d[1]) ** 2) - r).astype(np.float32)

def circle2(g, axis, P, r):
    u, v, _ = _coords(g, axis)
    return (np.sqrt((u - P[0]) ** 2 + (v - P[1]) ** 2) - r).astype(np.float32)

def rect2(g, axis, lo, hi, r=0.0):
    """2D rounded rectangle in the plane normal to axis (infinite along axis)"""
    u, v, _ = _coords(g, axis)
    lo = np.array(lo, np.float32) + r; hi = np.array(hi, np.float32) - r
    c = (lo + hi) / 2; e = (hi - lo) / 2
    qu = np.abs(u - c[0]) - e[0]; qv = np.abs(v - c[1]) - e[1]
    return (np.sqrt(np.maximum(qu, 0) ** 2 + np.maximum(qv, 0) ** 2) + np.minimum(np.maximum(qu, qv), 0) - r).astype(np.float32)

# ---------------- STL IO ----------------
def load_stl(fn):
    d = open(fn, 'rb').read(); n = struct.unpack('<I', d[80:84])[0]
    a = np.frombuffer(d[84:84 + 50 * n], dtype=np.dtype([('n', '<3f4'), ('v', '<9f4'), ('a', '<u2')]))
    return a['v'].reshape(-1, 3, 3).astype(np.float64)

def save_stl(fn, T, name=b'cardputer-case'):
    T = np.asarray(T, np.float32)
    n = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]); n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-20
    rec = np.zeros(len(T), dtype=np.dtype([('n', '<3f4'), ('v', '<9f4'), ('a', '<u2')]))
    rec['n'] = n; rec['v'] = T.reshape(-1, 9)
    with open(fn, 'wb') as f:
        f.write(name.ljust(80, b' ')); f.write(struct.pack('<I', len(T))); f.write(rec.tobytes())

# ---------------- mesh -> SDF ----------------
def _pt_tri_dist(P, a, b, c):
    """vectorised unsigned distance from points P (N,3) to triangle abc (Ericson)."""
    ab = b - a; ac = c - a; ap = P - a
    d1 = ap @ ab; d2 = ap @ ac
    bp = P - b; d3 = bp @ ab; d4 = bp @ ac
    cp = P - c; d5 = cp @ ab; d6 = cp @ ac
    va = d3 * d6 - d5 * d4; vb = d5 * d2 - d1 * d6; vc = d1 * d4 - d3 * d2
    denom = va + vb + vc; denom = np.where(np.abs(denom) < 1e-20, 1e-20, denom)
    v = vb / denom; w = vc / denom
    Cp = a + v[:, None] * ab + w[:, None] * ac  # interior default
    # edges
    m = (vc <= 0) & (d1 >= 0) & (d3 <= 0); t = d1 / np.where(d1 - d3 == 0, 1e-20, d1 - d3)
    Cp = np.where(m[:, None], a + t[:, None] * ab, Cp)
    m = (vb <= 0) & (d2 >= 0) & (d6 <= 0); t = d2 / np.where(d2 - d6 == 0, 1e-20, d2 - d6)
    Cp = np.where(m[:, None], a + t[:, None] * ac, Cp)
    m = (va <= 0) & ((d4 - d3) >= 0) & ((d5 - d6) >= 0)
    t = (d4 - d3) / np.where((d4 - d3) + (d5 - d6) == 0, 1e-20, (d4 - d3) + (d5 - d6))
    Cp = np.where(m[:, None], b + t[:, None] * (c - b), Cp)
    # vertices
    Cp = np.where(((d1 <= 0) & (d2 <= 0))[:, None], a, Cp)
    Cp = np.where(((d3 >= 0) & (d4 <= d3))[:, None], b, Cp)
    Cp = np.where(((d6 >= 0) & (d5 <= d6))[:, None], c, Cp)
    return np.linalg.norm(P - Cp, axis=1)

def mesh_sdf(g, T, band=1.0):
    """signed distance (clipped to +-band) of a closed triangle mesh T (n,3,3)."""
    h = g.h; o = g.o; n = g.n
    # occupancy by z-ray parity
    flip = np.zeros((n[0], n[1], n[2] + 1), np.int8)
    xs = g.X.ravel().astype(np.float64); ys = g.Y.ravel().astype(np.float64)
    for t in T:
        lo = t.min(0); hi = t.max(0)
        i0 = max(int(np.ceil((lo[0] - o[0]) / h)), 0); i1 = min(int(np.floor((hi[0] - o[0]) / h)), n[0] - 1)
        j0 = max(int(np.ceil((lo[1] - o[1]) / h)), 0); j1 = min(int(np.floor((hi[1] - o[1]) / h)), n[1] - 1)
        if i1 < i0 or j1 < j0: continue
        PX, PY = np.meshgrid(xs[i0:i1 + 1], ys[j0:j1 + 1], indexing='ij')
        a, b, c = t
        den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(den) < 1e-14: continue
        l1 = ((b[1] - c[1]) * (PX - c[0]) + (c[0] - b[0]) * (PY - c[1])) / den
        l2 = ((c[1] - a[1]) * (PX - c[0]) + (a[0] - c[0]) * (PY - c[1])) / den
        l3 = 1 - l1 - l2
        m = (l1 >= 0) & (l2 >= 0) & (l3 >= 0)
        if not m.any(): continue
        zh = l1 * a[2] + l2 * b[2] + l3 * c[2]
        ii, jj = np.nonzero(m)
        k = np.clip(np.ceil((zh[m] - o[2]) / h).astype(int), 0, n[2])
        np.add.at(flip, (ii + i0, jj + j0, k), 1)
    inside = (np.cumsum(flip, axis=2, dtype=np.int16)[:, :, :n[2]] & 1).astype(bool)
    del flip
    # unsigned distance in a band
    ud = np.full(tuple(n), band, np.float32)
    for t in T:
        lo = t.min(0) - band; hi = t.max(0) + band
        i0 = max(int(np.ceil((lo[0] - o[0]) / h)), 0); i1 = min(int(np.floor((hi[0] - o[0]) / h)), n[0] - 1)
        j0 = max(int(np.ceil((lo[1] - o[1]) / h)), 0); j1 = min(int(np.floor((hi[1] - o[1]) / h)), n[1] - 1)
        k0 = max(int(np.ceil((lo[2] - o[2]) / h)), 0); k1 = min(int(np.floor((hi[2] - o[2]) / h)), n[2] - 1)
        if i1 < i0 or j1 < j0 or k1 < k0: continue
        I_, J_, K_ = np.meshgrid(np.arange(i0, i1 + 1), np.arange(j0, j1 + 1), np.arange(k0, k1 + 1), indexing='ij')
        P = np.stack([o[0] + h * I_.ravel(), o[1] + h * J_.ravel(), o[2] + h * K_.ravel()], 1)
        d = _pt_tri_dist(P, t[0], t[1], t[2]).astype(np.float32).reshape(I_.shape)
        sub = ud[i0:i1 + 1, j0:j1 + 1, k0:k1 + 1]
        np.minimum(sub, d, out=sub)
    return np.where(inside, -ud, ud)

# ---------------- surface nets ----------------
def surface_nets(g, f):
    f = np.asarray(f, np.float32)
    s = f < 0
    n = np.array(f.shape); h = g.h; o = g.o
    cn = n - 1  # cells
    pts_cell = []; pts_xyz = []; faces = []
    for a in range(3):
        sl0 = [slice(None)] * 3; sl1 = [slice(None)] * 3
        sl0[a] = slice(0, -1); sl1[a] = slice(1, None)
        e = s[tuple(sl0)] != s[tuple(sl1)]
        idx = np.argwhere(e)
        if len(idx) == 0: continue
        i, j, k = idx.T
        f0 = f[i, j, k]; ii = [i, j, k]; ii[a] = ii[a] + 1; f1 = f[tuple(ii)]
        t = f0 / (f0 - f1)
        p = o + h * idx.astype(np.float64); p[:, a] += h * t
        b, c = (a + 1) % 3, (a + 2) % 3
        cells = []
        for db, dc in ((0, 0), (1, 0), (1, 1), (0, 1)):
            q = idx.copy(); q[:, b] -= db; q[:, c] -= dc
            cells.append(q)
        valid = np.ones(len(idx), bool)
        for q in cells:
            valid &= (q >= 0).all(1) & (q < cn).all(1)
        lin = [np.ravel_multi_index(q[valid].T, cn) for q in cells]
        for L in lin:
            pts_cell.append(L); pts_xyz.append(p[valid])
        quad = np.stack(lin, 1)
        inside_low = s[i, j, k][valid]
        # orient: cells ordered counter-clockwise around +a when viewed from +a; outward = +a if low end inside
        quad = np.where(inside_low[:, None], quad, quad[:, ::-1])
        faces.append(quad)
    pc = np.concatenate(pts_cell); px = np.concatenate(pts_xyz)
    uc, inv = np.unique(pc, return_inverse=True)
    cnt = np.bincount(inv).astype(np.float64)
    V = np.stack([np.bincount(inv, weights=px[:, d]) / cnt for d in range(3)], 1)
    Q = np.concatenate(faces)
    Q = np.searchsorted(uc, Q)
    # split quads along shorter diagonal
    P = V[Q]
    d02 = np.linalg.norm(P[:, 0] - P[:, 2], axis=1); d13 = np.linalg.norm(P[:, 1] - P[:, 3], axis=1)
    m = d02 <= d13
    T1 = np.where(m[:, None], Q[:, [0, 1, 2]], Q[:, [0, 1, 3]])
    T2 = np.where(m[:, None], Q[:, [0, 2, 3]], Q[:, [1, 2, 3]])
    F = np.concatenate([T1, T2])
    return V, F

def orient_check(V, F):
    """signed volume (should be >0 for outward normals)"""
    T = V[F]; return np.einsum('ij,ij->i', T[:, 0], np.cross(T[:, 1], T[:, 2])).sum() / 6
