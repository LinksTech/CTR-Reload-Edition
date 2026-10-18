# Creator Kit clips: the 14 animation clips of the Creator Kit puppet
# (idle, steer_left, steer_right, jump, drift_left, drift_right, hit,
# podium_1, podium_2, podium_3, lose, menu and the two optional clips
# finish_win, finish_lose). Everything here is made from
# code (pure maths, no random numbers, no clock, no game data) and released
# under CC0 (templates/LICENSE).
#
# Used by templates/make_creator_kit.py:
#
#   CLIP_NAMES, CLIPS                        the 14 names and (seconds, play, standing)
#   OPTIONAL                                 the clips a file may leave out without a warning
#   build_actions(rig, bones, info)          one Blender action per clip on the rig
#
# How a clip is made. For every frame 0..round(seconds * 30) a pose is
# described (forward-kinematic angles in degrees for the torso and head, a
# turn of the steering wheel, a pelvis offset, targets for hands and feet).
# A small solver turns it into the local rotation of every bone: it carries
# the grips with the steering wheel (kart_steer), reaches them with a
# two-bone solver in the arms, and plants the feet with a two-bone solver in
# the legs. All targets come from `bones` (the table of the rig), so a kit
# that moves the seat or the steering wheel gets matching clips.
#
# The rig stays in its seated bind pose as the rest pose; standing clips
# lift `pelvis` to standing height and straighten the legs (the Kart is
# hidden by the game / by the author in Blender for these clips). The finish
# clips (after the finish line, the kart drives on) are seated like the race
# clips: the pelvis stays on the seat.
#
# Units and axes. Model units "u": 64 u = 1 m. glTF axes: x = kart LEFT,
# y = up, z = kart FRONT. Blender: X = x / 64, Y = -z / 64, Z = y / 64.
# Angles: pitch + = forward (the head nods down), yaw + = turn toward +x
# (left), roll + = lean toward +x (left).

import math

import bpy
from mathutils import Matrix, Quaternion, Vector

UNIT = 64.0
FPS = 30

CLIP_NAMES = ["idle", "steer_left", "steer_right", "jump", "drift_left", "drift_right", "hit",
              "podium_1", "podium_2", "podium_3", "lose", "menu", "finish_win", "finish_lose"]

# Optional (format 4.3): played after the finish line, finish_win for places 1..3,
# finish_lose below. Without them the game plays what 4.2 played there.
OPTIONAL = ["finish_win", "finish_lose"]

# name: (duration in seconds, play mode of the game, standing without Kart)
CLIPS = {
    "idle": (2.0, "LOOP", False),
    "steer_left": (0.5, "POSE", False),
    "steer_right": (0.5, "POSE", False),
    "jump": (0.6, "ONCE", False),
    "drift_left": (1.0, "LOOP", False),
    "drift_right": (1.0, "LOOP", False),
    "hit": (1.0, "ONCE", False),
    "podium_1": (2.0, "LOOP", True),
    "podium_2": (2.0, "LOOP", True),
    "podium_3": (2.0, "LOOP", True),
    "lose": (2.0, "LOOP", True),
    "menu": (3.0, "LOOP", True),
    "finish_win": (2.0, "LOOP", False),
    "finish_lose": (2.0, "LOOP", False),
}

SIDES = (".L", ".R")
SIGN = {".L": 1.0, ".R": -1.0}
MARKER = "standing - hide Kart"

# The sole of the puppet's feet, in u relative to the ankle (foot) and to the
# ball joint (toe), in the bind pose (see the foot and toe shapes in
# creator_kit_puppet.py: foot axis 0.8 u below the ankle, half height 2.7, from
# 4.2 u behind to 8.6 u before the ankle; toe axis 2 u above the ball, half
# height 2.5, from 0.6 u behind to 5.4 u before the ball).
_FOOT_SOLE = [(0.0, -3.5, -4.2), (0.0, -3.5, 0.0), (0.0, -3.5, 4.0), (0.0, -3.5, 8.6)]
_TOE_SOLE = [(0.0, -0.5, -0.6), (0.0, -0.5, 2.0), (0.0, -0.5, 5.4)]

STAND_KNEE_RATIO = 0.975  # hip-to-ankle distance / leg length when standing (a hint of knee bend)
STAND_FOOT = (7.0, -2.0)  # ankle x (abs) and z of a standing foot, u


def Bv(p):
    return Vector((p[0] / UNIT, -p[2] / UNIT, p[1] / UNIT))


def Bdir(p):
    return Vector((p[0], -p[2], p[1]))


def _clamp(x, a=0.0, b=1.0):
    return a if x < a else b if x > b else x


def _smooth(x):
    x = _clamp(x)
    return x * x * (3.0 - 2.0 * x)


def _ramp(i, a, b):
    # 0 before frame a, 1 after frame b, smooth in between
    return _smooth((i - a) / float(b - a))


def _lerp(a, b, t):
    return a + (b - a) * t


def _sin(cycles, ph):
    return math.sin(2.0 * math.pi * cycles * ph)


def _cos(cycles, ph):
    return math.cos(2.0 * math.pi * cycles * ph)


# ------------------------------------------------------------------ solver


class _Solver:
    """Turns a pose description into the local rotation of every bone."""

    def __init__(self, rig, bones, info):
        arm = rig.data
        self.order = [b[0] for b in bones]
        self.parent = {b[0]: b[3] for b in bones}
        self.head_u = {b[0]: Vector(b[1]) for b in bones}
        self.rest = {n: Matrix(arm.bones[n].matrix_local) for n in self.order}
        self.restq = {n: self.rest[n].to_quaternion() for n in self.order}
        self.rel = {}
        for n in self.order:
            p = self.parent[n]
            self.rel[n] = (self.rest[p].inverted() @ self.rest[n]) if p else Matrix(self.rest[n])
        # the sign that turns the steering wheel "left" (its top toward +x)
        kb = [b for b in bones if b[0] == "kart_steer"][0]
        axis = (Vector(kb[2]) - Vector(kb[1])).normalized()
        up = Vector((0.0, 1.0, 0.0))
        uhat = (up - axis * up.dot(axis)).normalized()
        self.steer_sign = 1.0 if axis.cross(uhat).x > 0.0 else -1.0
        self.pole = {}
        self.L1 = {}
        self.L2 = {}
        self.Lhand = {}
        self.leg1 = {}
        self.leg2 = {}
        for s in SIDES:
            sh = self.rest["upper_arm" + s].translation
            el = self.rest["forearm" + s].translation
            wr = self.rest["hand" + s].translation
            dh = (wr - sh).normalized()
            p = (el - sh) - dh * (el - sh).dot(dh)
            self.pole[s] = p.normalized()
            self.L1[s] = (el - sh).length
            self.L2[s] = (wr - el).length
            self.Lhand[s] = (self.rest["fingers" + s].translation - wr).length
            hp = self.rest["thigh" + s].translation
            kn = self.rest["shin" + s].translation
            an = self.rest["foot" + s].translation
            self.leg1[s] = (kn - hp).length
            self.leg2[s] = (an - kn).length
        self.hip_u = {s: self.head_u["thigh" + s] for s in SIDES}
        self.ankle_u = {s: self.head_u["foot" + s] for s in SIDES}
        self.ball_u = {s: self.head_u["toe" + s] for s in SIDES}

    # ----- forward kinematics
    def pose_mats(self, basis):
        out = {}
        for n in self.order:
            loc, q = basis.get(n, (None, None))
            bm = q.to_matrix().to_4x4() if q is not None else Matrix.Identity(4)
            if loc is not None:
                bm.translation = loc
            p = self.parent[n]
            out[n] = (out[p] @ self.rel[n] @ bm) if p else (self.rest[n] @ bm)
        return out

    def fk(self, bone, pitch, yaw, roll):
        # pitch about +x, yaw about +y, roll (+ = toward +x) about -z, all in the rest axes
        q = (Quaternion(Bdir((0, 0, 1)), math.radians(-roll))
             @ Quaternion(Bdir((0, 1, 0)), math.radians(yaw))
             @ Quaternion(Bdir((1, 0, 0)), math.radians(pitch)))
        R = self.restq[bone]
        return R.inverted() @ q @ R

    # ----- two-bone solver
    @staticmethod
    def _two_bone(p0, target, l1, l2, pole):
        d = target - p0
        dist = min(max(d.length, abs(l1 - l2) + 1e-4), (l1 + l2) * 0.9995)
        dh = d.normalized()
        a = (l1 * l1 - l2 * l2 + dist * dist) / (2.0 * dist)
        h = math.sqrt(max(l1 * l1 - a * a, 0.0))
        perp = pole - dh * pole.dot(dh)
        if perp.length < 1e-6:
            perp = Vector((0.0, 0.0, 1.0)).cross(dh)
            if perp.length < 1e-6:
                perp = Vector((1.0, 0.0, 0.0)).cross(dh)
        perp.normalize()
        return p0 + dh * a + perp * h, p0 + dh * dist

    @staticmethod
    def _aim(M0, direction):
        # the minimal rotation that turns the y axis of the posed bone frame M0 to `direction`:
        # returns (local basis quaternion, world quaternion)
        Q0 = M0.to_quaternion()
        y0 = Q0 @ Vector((0.0, 1.0, 0.0))
        Qd = y0.rotation_difference(direction.normalized()) @ Q0
        return Q0.inverted() @ Qd, Qd

    def _basis_to(self, parent_pose, bone, Qt):
        # local basis that gives `bone` the world rotation Qt
        Qdef = (parent_pose @ self.rel[bone]).to_quaternion()
        return Qdef.inverted() @ Qt

    # ----- arms
    def _arm_item(self, side, mats, item):
        """(E, W, hand_target_quaternion or None) of one arm item."""
        kind = item[0]
        up = "upper_arm" + side
        sh = (mats[self.parent[up]] @ self.rel[up]).translation
        L1, L2 = self.L1[side], self.L2[side]
        if kind == "dirs":
            E = sh + L1 * Bdir(item[1]).normalized()
            W = E + L2 * Bdir(item[2]).normalized()
            return sh, E, W, None
        if kind == "grip":
            D = mats["kart_steer"] @ self.rest["kart_steer"].inverted()
            target = D @ self.rest["hand" + side].translation
            pole = (D.to_3x3() @ self.pole[side]).normalized()
            Qt = D.to_quaternion() @ self.restq["hand" + side]
            E, W = self._two_bone(sh, target, L1, L2, pole)
            return sh, E, W, Qt
        if kind == "hand":  # ("hand", fingers-head point u, hand direction, pole or None)
            dirB = Bdir(item[2]).normalized()
            target = Bv(item[1]) - dirB * self.Lhand[side]
            pole = Bdir(item[3]).normalized() if item[3] is not None else self.pole[side]
            E, W = self._two_bone(sh, target, L1, L2, pole)
            rest_y = self.restq["hand" + side] @ Vector((0.0, 1.0, 0.0))
            Qt = rest_y.rotation_difference(dirB) @ self.restq["hand" + side]
            return sh, E, W, Qt
        raise ValueError(kind)

    def _solve_arm(self, side, mats, items, basis):
        up, lo, hd = "upper_arm" + side, "forearm" + side, "hand" + side
        sh = None
        E = Vector((0.0, 0.0, 0.0))
        W = Vector((0.0, 0.0, 0.0))
        targets = []
        wsum = 0.0
        for item in items:
            w = item[0]
            if w <= 1e-9:
                continue
            sh, e, wr, qt = self._arm_item(side, mats, item[1:])
            E += w * e
            W += w * wr
            wsum += w
            targets.append((w, qt))
        if sh is None:
            return
        E /= wsum
        W /= wsum
        L1, L2 = self.L1[side], self.L2[side]
        E = sh + L1 * (E - sh).normalized()
        W = E + L2 * (W - E).normalized()
        M0 = mats[self.parent[up]] @ self.rel[up]
        b_up, Qd = self._aim(M0, E - sh)
        Mu = Matrix.Translation(sh) @ Qd.to_matrix().to_4x4()
        b_lo, Qd1 = self._aim(Mu @ self.rel[lo], W - E)
        basis[up] = (None, b_up)
        basis[lo] = (None, b_lo)
        Mf = Matrix.Translation(E) @ Qd1.to_matrix().to_4x4()
        acc = None
        wacc = 0.0
        for w, qt in targets:
            b = self._basis_to(Mf, hd, qt) if qt is not None else Quaternion((1.0, 0.0, 0.0, 0.0))
            if acc is None:
                acc, wacc = b, w
            else:
                acc = acc.slerp(b, w / (wacc + w))
                wacc += w
        basis[hd] = (None, acc)

    # ----- legs
    def _sole_min(self, side, pitch, toe):
        # lowest sole point relative to the ankle (Blender Z, metres) for a foot pitched by `pitch`
        # and a toe pitched by `pitch + toe` (degrees, + = toe down)
        Rf = Quaternion((1.0, 0.0, 0.0), math.radians(pitch))
        Rt = Quaternion((1.0, 0.0, 0.0), math.radians(pitch + toe))
        ball = Bv(self.ball_u[side]) - Bv(self.ankle_u[side])
        low = 1e9
        for p in _FOOT_SOLE:
            low = min(low, (Rf @ Bv(p)).z)
        for p in _TOE_SOLE:
            low = min(low, (Rf @ ball + Rt @ Bv(p)).z)
        return low

    def stand_pelvis(self):
        """Pelvis offset (u) of the standing pose with level feet and slightly bent knees."""
        s = ".L"
        ay = -self._sole_min(s, 0.0, 0.0) * UNIT
        ax, az = STAND_FOOT
        hip = self.hip_u[s]
        ls = (self.leg1[s] + self.leg2[s]) * UNIT * STAND_KNEE_RATIO
        hz_target = 0.0
        hx = hip.x  # unchanged
        horiz2 = (hx - ax) ** 2 + (hz_target - az) ** 2
        hy = ay + math.sqrt(max(ls * ls - horiz2, 0.0))
        return (0.0, hy - hip.y, hz_target - hip.z)

    def _solve_leg(self, side, mats, feet, basis):
        f = feet
        pitch, toe = f.get("pitch", 0.0), f.get("toe", 0.0)
        ax, lift, az = f["ankle"]
        ay = -self._sole_min(side, pitch, toe) * UNIT + lift
        target = Bv((ax * SIGN[side], ay, az))
        th, sn, ft, te = "thigh" + side, "shin" + side, "foot" + side, "toe" + side
        M0 = mats[self.parent[th]] @ self.rel[th]
        hip = M0.translation
        pole = Bdir(f.get("pole", (0.1 * SIGN[side], 0.0, 1.0))).normalized()
        K, A = self._two_bone(hip, target, self.leg1[side], self.leg2[side], pole)
        b_th, Qd = self._aim(M0, K - hip)
        Mt = Matrix.Translation(hip) @ Qd.to_matrix().to_4x4()
        b_sn, Qd1 = self._aim(Mt @ self.rel[sn], A - K)
        basis[th] = (None, b_th)
        basis[sn] = (None, b_sn)
        Ms = Matrix.Translation(K) @ Qd1.to_matrix().to_4x4()
        Qf = Quaternion((1.0, 0.0, 0.0), math.radians(pitch)) @ self.restq[ft]
        basis[ft] = (None, self._basis_to(Ms, ft, Qf))
        Mf = Matrix.Translation(A) @ Qf.to_matrix().to_4x4()
        Qt = Quaternion((1.0, 0.0, 0.0), math.radians(pitch + toe)) @ self.restq[te]
        basis[te] = (None, self._basis_to(Mf, te, Qt))

    # ----- the whole pose
    def solve(self, spec):
        basis = {}
        st = spec.get("steer", 0.0)
        basis["kart_steer"] = (None, Quaternion((0.0, 1.0, 0.0), math.radians(self.steer_sign * st)))
        for bone, (p, y, r) in spec.get("rot", {}).items():
            basis[bone] = (None, self.fk(bone, p, y, r))
        po = spec.get("pelvis")
        if po is not None:
            loc = self.restq["pelvis"].inverted() @ (Bdir(po) / UNIT)
            basis["pelvis"] = (loc, basis.get("pelvis", (None, None))[1])
        mats = self.pose_mats(basis)
        for s in SIDES:
            if s in spec.get("arms", {}):
                self._solve_arm(s, mats, spec["arms"][s], basis)
            if s in spec.get("feet", {}):
                self._solve_leg(s, mats, spec["feet"][s], basis)
        return basis


# ------------------------------------------------------------------ pose building blocks


def _base():
    return {"steer": 0.0, "rot": {}, "pelvis": None, "arms": {}, "feet": {}}


def _rot(sp, bone, pitch=0.0, yaw=0.0, roll=0.0):
    r = sp["rot"].setdefault(bone, [0.0, 0.0, 0.0])
    r[0] += pitch
    r[1] += yaw
    r[2] += roll


def _shoulders(sp, up_l, up_r):
    # raise (+) or lower (-) the shoulders by degrees
    _rot(sp, "shoulder.L", roll=-up_l)
    _rot(sp, "shoulder.R", roll=up_r)


def _grip_arms(sp):
    for s in SIDES:
        sp["arms"][s] = [(1.0, "grip")]


def _hang(sx, fwd=0.35):
    return ("dirs", (0.12 * sx, -1.0, 0.0), (0.08 * sx, -0.93, fwd))


def _vdir(sx, deg, fwd=0.0):
    a = math.radians(deg)
    return (sx * math.sin(a), math.cos(a), fwd)


# ------------------------------------------------------------------ seated clips


def _pose_idle(S, i, n):
    ph = i / float(n)
    sp = _base()
    _rot(sp, "chest", pitch=2.0 * _sin(1, ph))
    _rot(sp, "head", pitch=4.0 * _sin(2, ph), yaw=6.0 * _sin(1, ph))
    _shoulders(sp, 2.0 * _sin(1, ph), 2.0 * _sin(1, ph + 0.5))
    sp["pelvis"] = (0.0, 0.25 * (1.0 - _cos(2, ph)), 0.0)
    _grip_arms(sp)
    return sp


def _pose_steer(sign):
    def f(S, i, n):
        w = i / float(n)
        sp = _base()
        sp["steer"] = sign * 45.0 * w
        _rot(sp, "spine", roll=sign * 8.0 * w)
        _rot(sp, "chest", roll=sign * 10.0 * w)
        _rot(sp, "head", roll=sign * 10.0 * w, yaw=sign * 20.0 * w)
        _grip_arms(sp)
        return sp
    return f


def _pose_drift(sign):
    # sign +1 = drift left: counter-steer to the right, lean out to the right, look into the curve
    def f(S, i, n):
        ph = i / float(n)
        sp = _base()
        sp["steer"] = -sign * (45.0 + 5.0 * _sin(2, ph))
        _rot(sp, "spine", roll=-sign * 10.0)
        _rot(sp, "chest", roll=-sign * 15.0, pitch=2.0 * _sin(3, ph))
        _rot(sp, "head", roll=sign * 12.0, yaw=sign * 20.0, pitch=1.5 * _sin(3, ph + 0.25))
        _grip_arms(sp)
        return sp
    return f


def _pose_jump(S, i, n):
    sp = _base()
    crouch = _clamp(i / 4.0)
    v = _clamp((i - 4.0) / 14.0)
    e = 1.0 - (1.0 - v) * (1.0 - v)  # ease out
    chest = _lerp(15.0, -10.0, e) if i >= 4 else 15.0 * crouch
    head = _lerp(20.0, -12.0, e) if i >= 4 else 20.0 * crouch
    _rot(sp, "chest", pitch=chest)
    _rot(sp, "head", pitch=head)
    dy = -0.5 * crouch if i < 4 else _lerp(-0.5, 5.0, e)
    sp["pelvis"] = (0.0, dy, 0.0)
    wup = _ramp(i, 4, 12)
    for s in SIDES:
        sx = SIGN[s]
        up = ("dirs", _vdir(sx, 40.0, 0.12), _vdir(sx, 28.0, 0.1))
        sp["arms"][s] = [(1.0 - wup, "grip"), (wup, ) + up]
        _rot(sp, "thigh" + s, pitch=-15.0 * e)
        _rot(sp, "shin" + s, pitch=18.0 * e)
    _shoulders(sp, 8.0 * wup, 8.0 * wup)
    return sp


def _pose_hit(S, i, n):
    sp = _base()
    f = _ramp(i, 0, 4)  # flinch
    env = _ramp(i, 4, 8) * (1.0 - _ramp(i, 20, 26))  # dizzy circling
    s = (i - 4.0) / 20.0
    slump = _ramp(i, 22, 30)
    settle = 1.0 - _ramp(i, 4, 12)
    _rot(sp, "chest", pitch=-15.0 * f * settle + 12.0 * slump, roll=10.0 * _sin(1, s) * env)
    _rot(sp, "head", pitch=-20.0 * f * settle + 10.0 * slump,
         yaw=30.0 * _sin(2, s) * env, roll=15.0 * _cos(2, s) * env + (15.0 * slump))
    _shoulders(sp, 10.0 * f * (1.0 - _ramp(i, 18, 28)), 10.0 * f * (1.0 - _ramp(i, 18, 28)))
    dy = 4.0 * math.sin(math.pi * _clamp(i / 24.0))
    sp["pelvis"] = (0.0, dy, 0.0)
    wup = f * (1.0 - _ramp(i, 18, 28))
    for sd in SIDES:
        sx = SIGN[sd]
        up = ("dirs", _vdir(sx, 75.0, 0.05), _vdir(sx, 55.0, 0.1))
        sp["arms"][sd] = [(1.0 - wup, "grip"), (wup, ) + up]
    return sp


# ------------------------------------------------------------------ finish clips (seated)


def _pose_finish_win(S, i, n):
    # after the finish line on places 1..3: both fists up, pumped twice per loop, a small bounce in the seat
    ph = i / float(n)
    sp = _base()
    pump = _sin(2, ph)
    bounce = 0.5 - 0.5 * _cos(2, ph + 0.15)
    sp["pelvis"] = (0.0, 1.2 * bounce, 0.0)
    _rot(sp, "spine", pitch=-4.0)
    _rot(sp, "chest", pitch=-6.0 - 3.0 * bounce, roll=3.0 * _sin(1, ph))
    _rot(sp, "head", pitch=-14.0 - 4.0 * bounce, yaw=10.0 * _sin(1, ph), roll=-4.0 * _sin(1, ph))
    for s in SIDES:
        sx = SIGN[s]
        sp["arms"][s] = [(1.0, "dirs", _vdir(sx, 28.0 + 6.0 * pump, 0.12), _vdir(sx, 12.0 + 18.0 * pump, 0.1))]
    _shoulders(sp, 8.0 + 3.0 * pump, 8.0 + 3.0 * pump)
    return sp


def _pose_finish_lose(S, i, n):
    # after the finish line from place 4: slumped over the wheel, shoulders down, the head shakes slowly
    ph = i / float(n)
    sp = _base()
    sigh = 0.5 - 0.5 * _cos(1, ph)
    _rot(sp, "spine", pitch=4.0)
    _rot(sp, "chest", pitch=10.0 + 4.0 * sigh)
    _rot(sp, "head", pitch=22.0 + 4.0 * sigh, yaw=16.0 * _sin(2, ph), roll=3.0 * _sin(2, ph + 0.25))
    _shoulders(sp, -9.0 - 3.0 * sigh, -9.0 - 3.0 * sigh)
    sp["pelvis"] = (0.0, -0.4 * sigh, 0.0)
    _grip_arms(sp)
    return sp


# ------------------------------------------------------------------ standing clips


def _stand(S, sp, dy=0.0, shift=0.0, lifts=(0.0, 0.0), pitch=(0.0, 0.0), toe=(0.0, 0.0), foot_dz=(0.0, 0.0)):
    base = S.stand_pelvis()
    sp["pelvis"] = (base[0] + shift, base[1] + dy, base[2])
    for k, s in enumerate(SIDES):
        sp["feet"][s] = {"ankle": (STAND_FOOT[0], lifts[k], STAND_FOOT[1] + foot_dz[k]),
                         "pitch": pitch[k], "toe": toe[k]}
    return sp


def _hip_hand(S, sp, s, side_shift):
    # the hand on the hip: fingers head beside the hip joint, elbow out
    sx = SIGN[s]
    hip = S.hip_u[s] + Vector(sp["pelvis"])
    g = (hip.x + 7.5 * sx, hip.y + 5.0, hip.z + 3.0)
    return ("hand", g, (-0.35 * sx, -0.85, 0.4), (1.0 * sx, 0.0, -0.2))


def _pose_podium_1(S, i, n):
    ph = i / float(n)
    sp = _base()
    s2 = _sin(2, ph)
    hop = 8.0 * s2 if s2 > 0.0 else 3.0 * s2
    lift = 0.85 * max(hop, 0.0)
    _stand(S, sp, dy=hop, lifts=(lift, lift), pitch=(14.0 * lift / 8.0,) * 2, toe=(-6.0 * lift / 8.0,) * 2)
    _rot(sp, "chest", pitch=-5.0, roll=3.0 * _sin(2, ph + 0.25))
    _rot(sp, "head", pitch=-10.0, yaw=5.0 * _sin(1, ph))
    for k, s in enumerate(SIDES):
        sx = SIGN[s]
        pump = _sin(4, ph + 0.5 * k)
        sp["arms"][s] = [(1.0, "dirs", _vdir(sx, 32.0, 0.05), _vdir(sx, 32.0 + 28.0 * pump, 0.08))]
    _shoulders(sp, 6.0, 6.0)
    return sp


def _pose_podium_2(S, i, n):
    ph = i / float(n)
    sp = _base()
    sh = 1.5 * _sin(1, ph)
    _stand(S, sp, dy=-0.5 * (1.0 - _cos(2, ph)) * 0.5, shift=sh)
    _rot(sp, "chest", roll=-2.0 * _sin(1, ph), pitch=0.0)
    _rot(sp, "pelvis", roll=1.5 * _sin(1, ph))
    _rot(sp, "head", pitch=6.0 * _sin(2, ph), roll=4.0 * _sin(1, ph))
    wave = _sin(3, ph)
    sp["arms"][".R"] = [(1.0, "dirs", _vdir(-1.0, 68.0, 0.1), _vdir(-1.0, 6.0 + 30.0 * wave, 0.12))]
    sp["arms"][".L"] = [(1.0,) + _hip_hand(S, sp, ".L", 0.0)]
    return sp


def _pose_podium_3(S, i, n):
    ph = i / float(n)
    sp = _base()
    _stand(S, sp)
    clap = 0.5 - 0.5 * _cos(4, ph)  # closed 4 times
    shrug = math.exp(-((i - 36.0) / 5.0) ** 2)
    _rot(sp, "chest", pitch=3.0 * clap)
    _rot(sp, "head", roll=10.0, pitch=2.0 * clap, yaw=0.0)
    _shoulders(sp, 12.0 * shrug, 12.0 * shrug)
    y = S.head_u["upper_arm.L"].y + sp["pelvis"][1] - 8.0
    for s in SIDES:
        sx = SIGN[s]
        g = (_lerp(9.0, 1.0, clap) * sx, y, 14.0)
        sp["arms"][s] = [(1.0, "hand", g, (-0.25 * sx * (1.0 - clap), 0.3, 1.0), (sx, -0.8, 0.0))]
    return sp


def _pose_lose(S, i, n):
    ph = i / float(n)
    sp = _base()
    b = _smooth(math.sin(math.pi * _clamp((i - 14.0) / 30.0)))  # right foot scuff
    _stand(S, sp, dy=-1.0, pitch=(0.0, 16.0 * b), toe=(0.0, -10.0 * b), foot_dz=(0.0, 3.0 * b))
    # a slow sigh: the body sinks from a slightly slumped pose to the full droop and back
    droop = 0.35 + 0.65 * (0.5 - 0.5 * _cos(1, ph))
    _rot(sp, "chest", pitch=20.0 * droop)
    _rot(sp, "head", pitch=30.0 * droop, yaw=15.0 * _sin(1, ph))
    _shoulders(sp, -10.0, -10.0)
    for s in SIDES:
        sp["arms"][s] = [(1.0, ) + _hang(SIGN[s], 0.45)]
    return sp


def _pose_menu(S, i, n):
    ph = i / float(n)
    sp = _base()
    sh = 1.5 * _sin(1, ph)
    _stand(S, sp, dy=-0.5 * (1.0 - _cos(2, ph)) * 0.5, shift=sh)
    _rot(sp, "pelvis", roll=1.5 * _sin(1, ph))
    _rot(sp, "chest", roll=-2.0 * _sin(1, ph))
    _rot(sp, "head", yaw=25.0 * _sin(1, ph), pitch=2.0)
    env = _ramp(i, 15, 22) * (1.0 - _ramp(i, 38, 45))
    wv = _sin(3.0, _clamp((i - 15.0) / 30.0)) * env
    wave = ("dirs", _vdir(-1.0, 68.0, 0.1), _vdir(-1.0, 6.0 + 30.0 * wv, 0.12))
    sp["arms"][".R"] = [(1.0 - env, ) + _hip_hand(S, sp, ".R", 0.0), (env, ) + wave]
    sp["arms"][".L"] = [(1.0, ) + _hip_hand(S, sp, ".L", 0.0)]
    return sp


_POSES = {
    "idle": _pose_idle,
    "steer_left": _pose_steer(1.0),
    "steer_right": _pose_steer(-1.0),
    "jump": _pose_jump,
    "drift_left": _pose_drift(1.0),
    "drift_right": _pose_drift(-1.0),
    "hit": _pose_hit,
    "podium_1": _pose_podium_1,
    "podium_2": _pose_podium_2,
    "podium_3": _pose_podium_3,
    "lose": _pose_lose,
    "menu": _pose_menu,
    "finish_win": _pose_finish_win,
    "finish_lose": _pose_finish_lose,
}


# ------------------------------------------------------------------ actions


def _sample_clip(S, name):
    dur = CLIPS[name][0]
    n = int(round(dur * FPS))
    poses = []
    for i in range(n + 1):
        poses.append(S.solve(_POSES[name](S, i, n)))
    return n, poses


def _key(cb, path, index, group, frames, values):
    fc = cb.fcurves.new(path, index=index, group_name=group)
    fc.keyframe_points.add(len(frames))
    co = [0.0] * (2 * len(frames))
    co[0::2] = frames
    co[1::2] = values
    fc.keyframe_points.foreach_set("co", co)
    for kp in fc.keyframe_points:
        kp.interpolation = "LINEAR"
    fc.update()


def build_actions(rig, bones, info):
    S = _Solver(rig, bones, info)
    # the clips are made for 30 frames per second (the game's tick): the glTF exporter reads the scene rate
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.render.fps_base = 1.0
    rig.animation_data_create()
    figure = [n for n in S.order if n not in ("root", "kart", "kart_steer")]
    actions = []
    ident = Quaternion((1.0, 0.0, 0.0, 0.0))
    for name in CLIP_NAMES:
        n, poses = _sample_clip(S, name)
        frames = [float(i) for i in range(n + 1)]
        quats = {}
        for b in figure + ["kart_steer"]:
            seq = []
            for pz in poses:
                q = Quaternion(pz.get(b, (None, None))[1] or ident)
                q.normalize()
                if seq and q.dot(seq[-1]) < 0.0:
                    q.negate()
                seq.append(q)
            if seq[0].w < 0.0:
                seq = [Quaternion((-q.w, -q.x, -q.y, -q.z)) for q in seq]
            quats[b] = seq
        locs = [(pz.get("pelvis", (None, None))[0] or Vector((0.0, 0.0, 0.0))) for pz in poses]
        act = bpy.data.actions.new(name)
        slot = act.slots.new("OBJECT", rig.name)
        layer = act.layers.new("Layer")
        strip = layer.strips.new(type="KEYFRAME")
        cb = strip.channelbag(slot, ensure=True)
        for b in figure + ["kart_steer"]:
            seq = quats[b]
            moves = max(abs(q[k] - ident[k]) for q in seq for k in range(4)) > 1e-6
            if not (moves or b == "kart_steer"):
                continue
            for k in range(4):
                _key(cb, 'pose.bones["%s"].rotation_quaternion' % b, k, b, frames, [q[k] for q in seq])
        for k in range(3):
            _key(cb, 'pose.bones["pelvis"].location', k, "pelvis", frames, [l[k] for l in locs])
        act.use_fake_user = True
        act.use_frame_range = True
        act.frame_start = 0.0
        act.frame_end = float(n)
        if CLIPS[name][2]:
            mk = act.pose_markers.new(MARKER)
            mk.frame = 0
        actions.append(act)
    idle = actions[0]
    rig.animation_data.action = idle
    try:
        rig.animation_data.action_slot = idle.slots[0]
    except (AttributeError, TypeError, RuntimeError):
        pass
    for pb in rig.pose.bones:
        pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pb.location = (0.0, 0.0, 0.0)
    return actions
