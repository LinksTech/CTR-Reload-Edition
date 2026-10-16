"""Height queries on the drivable triangles of a model.

The triangles of the drivable classes go into a grid over x/z. A query at
(x, z) returns every triangle surface above or below that point; the caller
picks the layer it follows (the one nearest a reference height), so roads on
top of each other (bridges, an overpass) stay apart.

Pure Python and deterministic: the same triangles in the same order give the
same answers bit for bit.
"""

KIND_ROAD = 1
KIND_EDGE = 2

CELL = 256
EPS = 1e-7
DECAL = 5.0


class Surface:
    """Triangles by kind, indexed on an x/z grid of CELL units."""

    def __init__(self, cell=CELL):
        self.cell = cell
        self.tri = []      # (x0, y0, z0, e1x, e1y, e1z, e2x, e2y, e2z, inv_det, kind)
        self.grid = {}
        self.count = {KIND_ROAD: 0, KIND_EDGE: 0}
        self.skipped_vertical = 0
        self.min_y = None
        self.max_y = None

    def add(self, flat, kind):
        """Add triangles (flat sequence, 9 floats each) of one kind."""
        cell = self.cell
        grid = self.grid
        tri = self.tri
        for i in range(0, len(flat) - 8, 9):
            x0, y0, z0, x1, y1, z1, x2, y2, z2 = flat[i:i + 9]
            e1x, e1y, e1z = x1 - x0, y1 - y0, z1 - z0
            e2x, e2y, e2z = x2 - x0, y2 - y0, z2 - z0
            det = e1x * e2z - e2x * e1z
            if abs(det) < 1e-6:
                self.skipped_vertical += 1
                continue
            k = len(tri)
            tri.append((x0, y0, z0, e1x, e1y, e1z, e2x, e2y, e2z, 1.0 / det, kind))
            self.count[kind] = self.count.get(kind, 0) + 1
            lo_y = min(y0, y1, y2)
            hi_y = max(y0, y1, y2)
            self.min_y = lo_y if self.min_y is None else min(self.min_y, lo_y)
            self.max_y = hi_y if self.max_y is None else max(self.max_y, hi_y)
            ix0 = int(min(x0, x1, x2) // cell)
            ix1 = int(max(x0, x1, x2) // cell)
            iz0 = int(min(z0, z1, z2) // cell)
            iz1 = int(max(z0, z1, z2) // cell)
            for ix in range(ix0, ix1 + 1):
                for iz in range(iz0, iz1 + 1):
                    lst = grid.get((ix, iz))
                    if lst is None:
                        grid[(ix, iz)] = [k]
                    else:
                        lst.append(k)

    def hits(self, x, z):
        """Every surface over (x, z): a list of (y, kind), sorted by height."""
        lst = self.grid.get((int(x // self.cell), int(z // self.cell)))
        if not lst:
            return []
        out = []
        tri = self.tri
        for k in lst:
            x0, y0, z0, e1x, e1y, e1z, e2x, e2y, e2z, inv, kind = tri[k]
            px = x - x0
            pz = z - z0
            u = (px * e2z - pz * e2x) * inv
            if u < -EPS or u > 1 + EPS:
                continue
            v = (e1x * pz - e1z * px) * inv
            if v < -EPS or u + v > 1 + EPS:
                continue
            out.append((y0 + u * e1y + v * e2y, kind))
        out.sort()
        return out

    def surface(self, x, z, y_ref, window=192.0, kinds=(KIND_ROAD, KIND_EDGE)):
        """(y, kind) of the surface nearest y_ref within the window, or None.
        Where two triangles of the nearest layer overlap, road wins over edge.
        A surface at most DECAL above the nearest one (a decal on the road, a
        thin plate) is taken instead: the collision lies on the visible top."""
        hits = [(y, kind) for y, kind in self.hits(x, z) if kind in kinds and abs(y - y_ref) <= window + DECAL]
        best = None
        for y, kind in hits:
            d = abs(y - y_ref)
            if d > window:
                continue
            key = (round(d, 3), 0 if kind == KIND_ROAD else 1)
            if best is None or key < best[0]:
                best = (key, y, kind)
        if best is None:
            return None
        y, kind = best[1], best[2]
        for y2, kind2 in hits:
            if y < y2 <= best[1] + DECAL:
                y, kind = y2, kind2
        return y, kind
