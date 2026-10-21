#include <platform/native_extphys.h>
#include <math.h>

// THE EXTERNAL TRACK'S PHYSICS VALUES - see include/platform/native_extphys.h.
//
// The track's values are read once per level load into s_extPhys. Every hook
// first asks s_extPhys.p.anySet: 0 without --ext-track, without the section
// and with a neutral section (a neutral value is stored as "not set"), and the
// hook then hands its input back without touching it. Only a set value takes
// the other branch, and only while the track holds the level slot.
//
// The arithmetic of a set gravity share is the one of the MODS page
// (VehPhysForce.c, NativeMods_GravityPercent): gravity * percent / 100.
// A terrain row with own values is a copy of the game's row (the original
// table stays untouched, like NativeMods_GripTerrain), handed out in place of
// the row; fields not given keep the game's value.

enum
{
	EXT_PHYS_TERRAINS = 21,
	EXT_PHYS_GRAVITY_NEUTRAL = 100,
	EXT_PHYS_GRAVITY_MIN = 25,
	EXT_PHYS_GRAVITY_MAX = 400,
	EXT_PHYS_TERRAIN_VALUE_MIN = 0,
	EXT_PHYS_TERRAIN_VALUE_MAX = 1024,
	EXT_PHYS_PAD_RESERVES_NEUTRAL = 0x3c0, // VehPhysForce.c VEH_PHYS_FORCE_TURBO_PAD_RESERVES
	EXT_PHYS_PAD_RESERVES_MAX = 4000,
	EXT_PHYS_PAD_FIRE_NEUTRAL = 0x100, // VehPhysForce.c VEH_PHYS_FORCE_TURBO_PAD_FIRE_LEVEL
	EXT_PHYS_PAD_FIRE_MAX = 2048,
	EXT_PHYS_VERSION = 1,
	EXT_PHYS_ZONES_MAX = 16,
	EXT_PHYS_VOLUMES_MAX = 16,
	EXT_PHYS_ZONE_AIR_HOLD_MS = 1500,
	EXT_PHYS_ZONE_UP_BLEND = 64,
	EXT_PHYS_ZONE_MAX_UP_DEG = 150,
	// the source game's anti-gravity data (night run 1, cnk\report\poc\NL1_M1_SPEZ.md)
	EXT_PHYS_MAGLEV_MAX = 32,
	EXT_PHYS_MAGGRAV_MAX = 16,
	EXT_PHYS_SPLINES_MAX = 16,
	EXT_PHYS_SPLINE_NODES_MAX = 512,
	EXT_PHYS_LOW_SPEED_MAX = 32767,
};

// A box of the track (Reload units): centre, its own unit axes, half extents
// along them; a point is in it when |(p - centre) . axis| <= half on every
// axis (the source game's slab test 0x23CF10 takes its limits inclusive).
typedef struct
{
	double center[3];
	double axis[3][3];
	double half[3];
	char name[32];
} ExtPhysBox;

// the gravity types of a maggrav spline (eGRAVTYPE, NL1_M1_SPEZ.md D5)
enum
{
	EXT_PHYS_GRAV_PLANE = 0,
	EXT_PHYS_GRAV_POINT = 1,
	EXT_PHYS_GRAV_INVPOINT = 2,
};

typedef enum
{
	EXT_PHYS_TF_SPEED,         // speedMultiplier
	EXT_PHYS_TF_ACCEL,         // slowUntilSpeed
	EXT_PHYS_TF_COUNTER_STEER, // counterSteerRatio
	EXT_PHYS_TF_TURN_LEAN,     // turnLeanScale
	EXT_PHYS_TF_FRICTION,      // groundFrictionScale
	EXT_PHYS_TF_TURN_ANGLE,    // turnAngleScale
	EXT_PHYS_TF_TURN_RESPONSE, // turnResponseScale
	EXT_PHYS_TF_COUNT
} ExtPhysTerrainField;

global_variable const char *const s_extPhysFieldNames[EXT_PHYS_TF_COUNT] = {"speed",    "accel",      "counter_steer", "turn_lean",
                                                                            "friction", "turn_angle", "turn_response"};

// enum TerrainType (include/namespace_Level.h) by name
global_variable const char *const s_extPhysTerrainNames[EXT_PHYS_TERRAINS] = {
    "ASPHALT",  "DIRT",     "GRASS", "WOOD",      "WATER", "STONE",    "ICE",          "TRACK",        "ICY_ROAD",     "SNOW",     "NONE",
    "HARDPACK", "METAL",    "FASTWATER", "MUD",   "SIDESLIP", "RIVERASPHALT", "STEAMASPHALT", "OCEANASPHALT", "SLOWGRASS", "SLOWDIRT"};

typedef struct
{
	int present; // the section is there
	int anySet;  // at least one value differs from the game's own
	int gravitySet;
	int gravityPercent;
	int padReservesSet;
	int padReserves;
	int padFireSet;
	int padFireLevel;
	int terrainSet[EXT_PHYS_TERRAINS]; // bit per ExtPhysTerrainField
	int terrainValue[EXT_PHYS_TERRAINS][EXT_PHYS_TF_COUNT];
	// what the reader met (log only)
	int unknownKeys;
	int clamped;
	int badValues;
	int neutralGiven; // values given but equal to the game's own
	// the anti-gravity zones (B2): an entry per zone id the collision uses
	int zoneCount;
	struct
	{
		int id;             // 1..255, the "zone" of the collision nodes
		int gravityPercent; // the share of the game's gravity along the surface (25..400)
		int airHoldMs;      // how long the zone's gravity holds in the air (0..10000)
		int upBlend;        // the share of the new surface normal per frame, /256 (1..256)
		int maxUpDeg;       // the zone's up never tilts further from world up (0..180)
	} zones[EXT_PHYS_ZONES_MAX];
	// gravity volumes: boxes (any turn) in which the gravity has its own share
	int volumeCount;
	struct
	{
		double center[3];  // Reload units
		double axis[3][3]; // the box's own unit axes
		double half[3];    // half extents along them
		int gravityPercent;
		char name[32];
	} volumes[EXT_PHYS_VOLUMES_MAX];
	// the source game's maglev switching boxes (physics.maglev_volumes): a
	// driver outside a zone enters it in an "on" box, one in a zone leaves it
	// in an "off" box (NL1_M1_SPEZ.md Z2)
	int maglevCount;
	struct
	{
		ExtPhysBox box;
		int on; // 1 "on", 0 "off"
	} maglev[EXT_PHYS_MAGLEV_MAX];
	// the maggrav boxes (physics.maggrav_volumes) with the index of their
	// gravity spline (physics.maggrav_splines), and the speed under which a
	// driver on the ground keeps the zone's own gravity (maglev_low_speed, K4)
	int maggravCount;
	struct
	{
		ExtPhysBox box;
		int spline;
	} maggrav[EXT_PHYS_MAGGRAV_MAX];
	int splineCount;
	struct
	{
		int type;  // EXT_PHYS_GRAV_*, -1 an empty entry
		int first; // its first node in nodes[]
		int count; // its nodes (0: unusable)
	} splines[EXT_PHYS_SPLINES_MAX];
	int nodeCount;
	double nodes[EXT_PHYS_SPLINE_NODES_MAX][6]; // position (Reload units), direction
	int lowSpeedSet;
	int lowSpeed; // the game's speed units (d->speed)
} ExtPhysParams;

global_variable struct
{
	ExtPhysParams p;
	struct Terrain rows[EXT_PHYS_TERRAINS]; // the copies handed out for set rows
} s_extPhys;

internal int *ExtPhys_FieldOf(struct Terrain *t, int field)
{
	switch (field)
	{
	case EXT_PHYS_TF_SPEED:
		return &t->speedMultiplier;
	case EXT_PHYS_TF_ACCEL:
		return &t->slowUntilSpeed;
	case EXT_PHYS_TF_COUNTER_STEER:
		return &t->counterSteerRatio;
	case EXT_PHYS_TF_TURN_LEAN:
		return &t->turnLeanScale;
	case EXT_PHYS_TF_FRICTION:
		return &t->groundFrictionScale;
	case EXT_PHYS_TF_TURN_ANGLE:
		return &t->turnAngleScale;
	default:
		return &t->turnResponseScale;
	}
}

// An integer value of a key: 1 = read (clamped into [lo, hi] when outside), 0 =
// the key is missing, -1 = not a number.
internal int ExtPhys_ReadInt(const ExtJsonDoc *doc, int node, int lo, int hi, int *out, ExtPhysParams *p)
{
	double v;
	int i;

	if (node < 0)
	{
		return 0;
	}
	if (ExtJson_Type(doc, node) != EXT_JSON_NUMBER)
	{
		p->badValues++;
		return -1;
	}
	v = ExtJson_Number(doc, node, 0.0);
	i = (int)floor(v + 0.5);
	if ((double)i != v)
	{
		p->badValues++; // not a whole number: rounded, counted
	}
	if (i < lo)
	{
		i = lo;
		p->clamped++;
	}
	if (i > hi)
	{
		i = hi;
		p->clamped++;
	}
	*out = i;
	return 1;
}

internal int ExtPhys_TerrainIndex(const char *name)
{
	for (int t = 0; t < EXT_PHYS_TERRAINS; t++)
	{
		if (strcmp(name, s_extPhysTerrainNames[t]) == 0)
		{
			return t;
		}
	}
	return -1;
}

// A box {"name", "center": [3], "axes": [[3], [3], [3]], "half": [3]}: 1 read
// (axes normalised), 0 broken.
internal int ExtPhys_ReadBox(const ExtJsonDoc *doc, int node, ExtPhysBox *b)
{
	const int axes = (ExtJson_Type(doc, node) == EXT_JSON_OBJECT) ? ExtJson_Get(doc, node, "axes") : -1;
	int ok = (axes >= 0) && (ExtJson_Numbers(doc, ExtJson_Get(doc, node, "center"), b->center, 3) == 3) &&
	         (ExtJson_Numbers(doc, ExtJson_Get(doc, node, "half"), b->half, 3) == 3);

	for (int a = 0; ok && (a < 3); a++)
	{
		double len;

		ok = (ExtJson_Numbers(doc, ExtJson_At(doc, axes, a), b->axis[a], 3) == 3) && (b->half[a] > 0.0);
		len = ok ? sqrt(b->axis[a][0] * b->axis[a][0] + b->axis[a][1] * b->axis[a][1] + b->axis[a][2] * b->axis[a][2]) : 0.0;
		ok = ok && (len > 1e-6);
		for (int k = 0; ok && (k < 3); k++)
		{
			b->axis[a][k] /= len;
		}
	}
	if (ok)
	{
		snprintf(b->name, sizeof(b->name), "%s", ExtJson_String(doc, ExtJson_Get(doc, node, "name"), "box"));
	}
	return ok;
}

// Is point p in box b.
internal int ExtPhys_InBox(const ExtPhysBox *b, const double p[3])
{
	const double d[3] = {p[0] - b->center[0], p[1] - b->center[1], p[2] - b->center[2]};

	for (int a = 0; a < 3; a++)
	{
		if (fabs(d[0] * b->axis[a][0] + d[1] * b->axis[a][1] + d[2] * b->axis[a][2]) > b->half[a])
		{
			return 0;
		}
	}
	return 1;
}

// physics.maglev_volumes, physics.maggrav_volumes, physics.maggrav_splines,
// physics.maglev_low_speed (tr1_phys.py antigrav_section).
internal void ExtPhys_ReadAntigrav(const ExtJsonDoc *doc, const char *name, int key, ExtPhysParams *p)
{
	if (strcmp(name, "maglev_volumes") == 0)
	{
		for (int g = ExtJson_First(doc, key); g >= 0; g = ExtJson_Next(doc, g))
		{
			const char *kind = ExtJson_String(doc, ExtJson_Get(doc, g, "kind"), "");
			const int on = (strcmp(kind, "on") == 0) ? 1 : ((strcmp(kind, "off") == 0) ? 0 : -1);

			if ((p->maglevCount >= EXT_PHYS_MAGLEV_MAX) || (on < 0) || !ExtPhys_ReadBox(doc, g, &p->maglev[p->maglevCount].box))
			{
				p->badValues++;
				continue;
			}
			p->maglev[p->maglevCount].on = on;
			p->maglevCount++;
		}
	}
	else if (strcmp(name, "maggrav_volumes") == 0)
	{
		for (int g = ExtJson_First(doc, key); g >= 0; g = ExtJson_Next(doc, g))
		{
			int spline = -1;

			if ((p->maggravCount >= EXT_PHYS_MAGGRAV_MAX) || !ExtPhys_ReadBox(doc, g, &p->maggrav[p->maggravCount].box) ||
			    (ExtPhys_ReadInt(doc, ExtJson_Get(doc, g, "spline"), 0, EXT_PHYS_SPLINES_MAX - 1, &spline, p) != 1))
			{
				p->badValues++;
				continue;
			}
			p->maggrav[p->maggravCount].spline = spline;
			p->maggravCount++;
		}
	}
	else if (strcmp(name, "maggrav_splines") == 0)
	{
		for (int s = ExtJson_First(doc, key); (s >= 0) && (p->splineCount < EXT_PHYS_SPLINES_MAX); s = ExtJson_Next(doc, s))
		{
			const int nodes = (ExtJson_Type(doc, s) == EXT_JSON_OBJECT) ? ExtJson_Get(doc, s, "nodes") : -1;
			const int n = (nodes >= 0) ? ExtJson_Count(doc, nodes) : 0;
			int type = -1;
			int ok = 1;

			// the entry keeps its place in the list (the index of maggrav<NN>)
			p->splines[p->splineCount].type = -1;
			p->splines[p->splineCount].first = p->nodeCount;
			p->splines[p->splineCount].count = 0;
			// (a type outside -1..2 is no gravity type: not clamped, the entry unusable)
			if ((ExtJson_Type(doc, s) == EXT_JSON_OBJECT) && (ExtPhys_ReadInt(doc, ExtJson_Get(doc, s, "type"), -1000, 1000, &type, p) == 1) &&
			    ((type < -1) || (type > EXT_PHYS_GRAV_INVPOINT)))
			{
				type = -1;
				ok = 0;
			}
			if (ok && (type >= 0) && (n >= 2) && (p->nodeCount + n <= EXT_PHYS_SPLINE_NODES_MAX))
			{
				for (int k = 0; ok && (k < n); k++)
				{
					ok = ExtJson_Numbers(doc, ExtJson_At(doc, nodes, k), p->nodes[p->nodeCount + k], 6) == 6;
				}
				if (ok)
				{
					p->splines[p->splineCount].type = type;
					p->splines[p->splineCount].count = n;
					p->nodeCount += n;
				}
				else
				{
					p->badValues++;
				}
			}
			else if ((type >= 0) || !ok)
			{
				p->badValues++;
			}
			p->splineCount++;
		}
	}
	else
	{
		int v = 0;

		if (ExtPhys_ReadInt(doc, key, 1, EXT_PHYS_LOW_SPEED_MAX, &v, p) == 1)
		{
			p->lowSpeedSet = 1;
			p->lowSpeed = v;
		}
	}
}

// Reads the section (or its absence) into p. A value equal to the game's own
// is not stored: p then equals the one of a missing section.
internal void ExtPhys_Read(const ExtJsonDoc *doc, int section, ExtPhysParams *p)
{
	int v = 0;

	memset(p, 0, sizeof(*p));
	if ((doc == NULL) || (section < 0) || (ExtJson_Type(doc, section) != EXT_JSON_OBJECT))
	{
		return;
	}
	p->present = 1;

	for (int key = ExtJson_First(doc, section); key >= 0; key = ExtJson_Next(doc, key))
	{
		const char *name = doc->nodes[key].key;

		if (name == NULL)
		{
			continue;
		}
		if (strcmp(name, "version") == 0)
		{
			if ((ExtPhys_ReadInt(doc, key, 0, 1000, &v, p) == 1) && (v != EXT_PHYS_VERSION))
			{
				p->badValues++;
			}
		}
		else if (strcmp(name, "gravity_percent") == 0)
		{
			if (ExtPhys_ReadInt(doc, key, EXT_PHYS_GRAVITY_MIN, EXT_PHYS_GRAVITY_MAX, &v, p) == 1)
			{
				if (v == EXT_PHYS_GRAVITY_NEUTRAL)
				{
					p->neutralGiven++;
				}
				else
				{
					p->gravitySet = 1;
					p->gravityPercent = v;
				}
			}
		}
		else if (strcmp(name, "turbo_pad") == 0)
		{
			for (int k = ExtJson_First(doc, key); k >= 0; k = ExtJson_Next(doc, k))
			{
				const char *kn = doc->nodes[k].key;

				if ((kn != NULL) && (strcmp(kn, "reserves") == 0))
				{
					if (ExtPhys_ReadInt(doc, k, 0, EXT_PHYS_PAD_RESERVES_MAX, &v, p) == 1)
					{
						if (v == EXT_PHYS_PAD_RESERVES_NEUTRAL)
						{
							p->neutralGiven++;
						}
						else
						{
							p->padReservesSet = 1;
							p->padReserves = v;
						}
					}
				}
				else if ((kn != NULL) && (strcmp(kn, "fire_level") == 0))
				{
					if (ExtPhys_ReadInt(doc, k, 0, EXT_PHYS_PAD_FIRE_MAX, &v, p) == 1)
					{
						if (v == EXT_PHYS_PAD_FIRE_NEUTRAL)
						{
							p->neutralGiven++;
						}
						else
						{
							p->padFireSet = 1;
							p->padFireLevel = v;
						}
					}
				}
				else
				{
					p->unknownKeys++;
				}
			}
		}
		else if (strcmp(name, "terrain") == 0)
		{
			for (int row = ExtJson_First(doc, key); row >= 0; row = ExtJson_Next(doc, row))
			{
				const char *rn = doc->nodes[row].key;
				const int t = (rn != NULL) ? ExtPhys_TerrainIndex(rn) : -1;

				if (t < 0)
				{
					p->unknownKeys++;
					continue;
				}
				for (int k = ExtJson_First(doc, row); k >= 0; k = ExtJson_Next(doc, k))
				{
					const char *kn = doc->nodes[k].key;
					int field = -1;

					for (int f = 0; (kn != NULL) && (f < EXT_PHYS_TF_COUNT); f++)
					{
						if (strcmp(kn, s_extPhysFieldNames[f]) == 0)
						{
							field = f;
						}
					}
					if (field < 0)
					{
						p->unknownKeys++;
						continue;
					}
					if (ExtPhys_ReadInt(doc, k, EXT_PHYS_TERRAIN_VALUE_MIN, EXT_PHYS_TERRAIN_VALUE_MAX, &v, p) == 1)
					{
						if (v == *ExtPhys_FieldOf(&data.MetaDataTerrain[t], field))
						{
							p->neutralGiven++;
						}
						else
						{
							p->terrainSet[t] |= 1 << field;
							p->terrainValue[t][field] = v;
						}
					}
				}
			}
		}
		else if (strcmp(name, "gravity_volumes") == 0)
		{
			// [{"name", "center": [x,y,z], "axes": [[3],[3],[3]], "half": [3], "gravity_percent"}]
			for (int g = ExtJson_First(doc, key); g >= 0; g = ExtJson_Next(doc, g))
			{
				double c[3], ax[3][3], h[3];
				int ok = (ExtJson_Type(doc, g) == EXT_JSON_OBJECT) && (p->volumeCount < EXT_PHYS_VOLUMES_MAX);
				const int axes = ok ? ExtJson_Get(doc, g, "axes") : -1;
				int pct = EXT_PHYS_GRAVITY_NEUTRAL;

				ok = ok && (ExtJson_Numbers(doc, ExtJson_Get(doc, g, "center"), c, 3) == 3) && (ExtJson_Numbers(doc, ExtJson_Get(doc, g, "half"), h, 3) == 3);
				for (int a = 0; ok && (a < 3); a++)
				{
					double len;

					ok = (ExtJson_Numbers(doc, ExtJson_At(doc, axes, a), ax[a], 3) == 3) && (h[a] > 0.0);
					len = ok ? sqrt(ax[a][0] * ax[a][0] + ax[a][1] * ax[a][1] + ax[a][2] * ax[a][2]) : 0.0;
					ok = ok && (len > 1e-6);
					for (int k = 0; ok && (k < 3); k++)
					{
						ax[a][k] /= len;
					}
				}
				ok = ok && (ExtPhys_ReadInt(doc, ExtJson_Get(doc, g, "gravity_percent"), EXT_PHYS_GRAVITY_MIN, EXT_PHYS_GRAVITY_MAX, &pct, p) == 1);
				if (!ok)
				{
					p->badValues++;
					continue;
				}
				memcpy(p->volumes[p->volumeCount].center, c, sizeof(c));
				memcpy(p->volumes[p->volumeCount].axis, ax, sizeof(ax));
				memcpy(p->volumes[p->volumeCount].half, h, sizeof(h));
				p->volumes[p->volumeCount].gravityPercent = pct;
				snprintf(p->volumes[p->volumeCount].name, sizeof(p->volumes[p->volumeCount].name), "%s",
				         ExtJson_String(doc, ExtJson_Get(doc, g, "name"), "volume"));
				p->volumeCount++;
			}
		}
		else if (strcmp(name, "zones") == 0)
		{
			// the anti-gravity zones: [{"id", "gravity_percent", "air_hold_ms", "up_blend", "max_up_deg"}]
			for (int z = ExtJson_First(doc, key); z >= 0; z = ExtJson_Next(doc, z))
			{
				const int idNode = (ExtJson_Type(doc, z) == EXT_JSON_OBJECT) ? ExtJson_Get(doc, z, "id") : -1;
				const double idValue = (ExtJson_Type(doc, idNode) == EXT_JSON_NUMBER) ? ExtJson_Number(doc, idNode, 0.0) : 0.0;
				const int id = (int)idValue;

				// the id names the collision's "zone": exactly 1..255, never clamped
				if ((idNode < 0) || ((double)id != idValue) || (id < 1) || (id > 255) || (p->zoneCount >= EXT_PHYS_ZONES_MAX))
				{
					p->badValues++;
					continue;
				}
				p->zones[p->zoneCount].id = id;
				p->zones[p->zoneCount].gravityPercent = EXT_PHYS_GRAVITY_NEUTRAL;
				p->zones[p->zoneCount].airHoldMs = EXT_PHYS_ZONE_AIR_HOLD_MS;
				p->zones[p->zoneCount].upBlend = EXT_PHYS_ZONE_UP_BLEND;
				p->zones[p->zoneCount].maxUpDeg = EXT_PHYS_ZONE_MAX_UP_DEG;
				ExtPhys_ReadInt(doc, ExtJson_Get(doc, z, "gravity_percent"), EXT_PHYS_GRAVITY_MIN, EXT_PHYS_GRAVITY_MAX, &p->zones[p->zoneCount].gravityPercent,
				                p);
				ExtPhys_ReadInt(doc, ExtJson_Get(doc, z, "air_hold_ms"), 0, 10000, &p->zones[p->zoneCount].airHoldMs, p);
				ExtPhys_ReadInt(doc, ExtJson_Get(doc, z, "up_blend"), 1, 256, &p->zones[p->zoneCount].upBlend, p);
				ExtPhys_ReadInt(doc, ExtJson_Get(doc, z, "max_up_deg"), 0, 180, &p->zones[p->zoneCount].maxUpDeg, p);
				for (int k = ExtJson_First(doc, z); k >= 0; k = ExtJson_Next(doc, k))
				{
					const char *kn = doc->nodes[k].key;

					if ((kn != NULL) && (strcmp(kn, "id") != 0) && (strcmp(kn, "gravity_percent") != 0) && (strcmp(kn, "air_hold_ms") != 0) &&
					    (strcmp(kn, "up_blend") != 0) && (strcmp(kn, "max_up_deg") != 0))
					{
						p->unknownKeys++;
					}
				}
				p->zoneCount++;
			}
		}
		else if ((strcmp(name, "maglev_volumes") == 0) || (strcmp(name, "maggrav_volumes") == 0) || (strcmp(name, "maggrav_splines") == 0) ||
		         (strcmp(name, "maglev_low_speed") == 0))
		{
			ExtPhys_ReadAntigrav(doc, name, key, p);
		}
		else
		{
			p->unknownKeys++;
		}
	}

	// a zone asks for a behaviour of its own: never neutral
	p->anySet = p->gravitySet || p->padReservesSet || p->padFireSet || (p->zoneCount > 0) || (p->volumeCount > 0);
	for (int t = 0; t < EXT_PHYS_TERRAINS; t++)
	{
		if (p->terrainSet[t] != 0)
		{
			p->anySet = 1;
		}
	}
}

// The copies of the rows with own values (the others are never handed out).
internal void ExtPhys_MakeRows(const ExtPhysParams *p, struct Terrain rows[EXT_PHYS_TERRAINS])
{
	for (int t = 0; t < EXT_PHYS_TERRAINS; t++)
	{
		rows[t] = data.MetaDataTerrain[t];
		for (int f = 0; f < EXT_PHYS_TF_COUNT; f++)
		{
			if ((p->terrainSet[t] & (1 << f)) != 0)
			{
				*ExtPhys_FieldOf(&rows[t], f) = p->terrainValue[t][f];
			}
		}
	}
}

// the hooks on given parameters (the selftest calls these directly)

internal int ExtPhys_ScaleGravityWith(const ExtPhysParams *p, int gravity)
{
	if (!p->gravitySet)
	{
		return gravity;
	}
	return CTR_MipsDiv(CTR_MipsMulLo(gravity, p->gravityPercent), EXT_PHYS_GRAVITY_NEUTRAL);
}

internal struct Terrain *ExtPhys_TerrainWith(const ExtPhysParams *p, struct Terrain rows[EXT_PHYS_TERRAINS], unsigned char terrainType,
                                             struct Terrain *row)
{
	// only the game's own row of that type is replaced (a type out of range
	// gets row 0 from VehAfterColl_GetTerrain and keeps it)
	if ((terrainType >= EXT_PHYS_TERRAINS) || (p->terrainSet[terrainType] == 0) || (row != &data.MetaDataTerrain[terrainType]))
	{
		return row;
	}
	return &rows[terrainType];
}

// Is the read set in force: values set, and the track holds the running level.
internal int ExtPhys_InForce(void)
{
	const struct GameTracker *gGT;

	if (!s_extPhys.p.anySet)
	{
		return 0;
	}
	gGT = sdata->gGT;
	return (gGT != NULL) && ExtTrack_ActiveForLevel(gGT->levelID);
}

int ExtPhys_ScaleGravity(int gravity)
{
	if (!s_extPhys.p.anySet || !ExtPhys_InForce())
	{
		return gravity;
	}
	return ExtPhys_ScaleGravityWith(&s_extPhys.p, gravity);
}

struct Terrain *ExtPhys_Terrain(unsigned char terrainType, struct Terrain *row)
{
	if (!s_extPhys.p.anySet || !ExtPhys_InForce())
	{
		return row;
	}
	return ExtPhys_TerrainWith(&s_extPhys.p, s_extPhys.rows, terrainType, row);
}

int ExtPhys_PadReserves(int reserves)
{
	if (!s_extPhys.p.padReservesSet || !ExtPhys_InForce())
	{
		return reserves;
	}
	return s_extPhys.p.padReserves;
}

int ExtPhys_PadFireLevel(int fireLevel)
{
	if (!s_extPhys.p.padFireSet || !ExtPhys_InForce())
	{
		return fireLevel;
	}
	return s_extPhys.p.padFireLevel;
}

void ExtPhys_OnLevelLoaded(void)
{
	const ExtJsonDoc *json = ExtTrack_Json();
	const ExtPhysParams *p = &s_extPhys.p;
	int rows = 0;

	ExtPhys_Read(json, (json != NULL) ? ExtTrack_Section("physics") : -1, &s_extPhys.p);
	ExtPhys_MakeRows(&s_extPhys.p, s_extPhys.rows);
	for (int t = 0; t < EXT_PHYS_TERRAINS; t++)
	{
		if (p->terrainSet[t] != 0)
		{
			rows++;
		}
	}
	// the turbo pads of the collision (records of class trigger), one line each
	for (int pad = 0; pad < ExtColl_PadCount(); pad++)
	{
		struct BoundingBox box;
		int records = 0, bits = 0;

		if (ExtColl_PadBox(pad, &box, &records, &bits))
		{
			Platform_Log("[CTR ExtPhys] pad %d box %d %d %d %d %d %d (%d record(s), %s)\n", pad, box.min.x, box.min.y, box.min.z, box.max.x, box.max.y,
			             box.max.z, records, (bits == 2) ? "super pad" : "turbo pad");
		}
	}
	if (!p->present)
	{
		Platform_Log("[CTR ExtPhys] physics: no section - the game's own values\n");
		return;
	}
	Platform_Log("[CTR ExtPhys] physics: gravity %d%%%s, terrain rows with own values %d, turbo pad reserves %d fire level %d%s; %s; %d neutral value(s) kept "
	             "as not set, %d unknown key(s), %d value(s) clamped, %d bad value(s)\n",
	             p->gravitySet ? p->gravityPercent : EXT_PHYS_GRAVITY_NEUTRAL, p->gravitySet ? "" : " (neutral)", rows,
	             p->padReservesSet ? p->padReserves : EXT_PHYS_PAD_RESERVES_NEUTRAL, p->padFireSet ? p->padFireLevel : EXT_PHYS_PAD_FIRE_NEUTRAL,
	             (p->padReservesSet || p->padFireSet) ? "" : " (neutral)", p->anySet ? "own values in force" : "all neutral - the game's own values",
	             p->neutralGiven, p->unknownKeys, p->clamped, p->badValues);
	Platform_Log("[CTR ExtPhys] zones %d, gravity volumes %d\n", p->zoneCount, p->volumeCount);
	if ((p->maglevCount > 0) || (p->maggravCount > 0) || (p->splineCount > 0) || p->lowSpeedSet)
	{
		int on = 0;

		for (int v = 0; v < p->maglevCount; v++)
		{
			on += p->maglev[v].on;
		}
		Platform_Log("[CTR ExtPhys] maglev volumes %d (on %d, off %d), maggrav volumes %d, splines %d (%d nodes), low speed %d%s\n", p->maglevCount, on,
		             p->maglevCount - on, p->maggravCount, p->splineCount, p->nodeCount, p->lowSpeedSet ? p->lowSpeed : 0, p->lowSpeedSet ? "" : " (not set)");
		for (int s = 0; s < p->splineCount; s++)
		{
			Platform_Log("[CTR ExtPhys] maggrav spline %d: type %d, %d node(s)\n", s, p->splines[s].type, p->splines[s].count);
		}
	}
	for (int v = 0; v < p->volumeCount; v++)
	{
		Platform_Log("[CTR ExtPhys] gravity volume %d (%s): %d%%, centre %.0f %.0f %.0f, half %.0f %.0f %.0f\n", v, p->volumes[v].name, p->volumes[v].gravityPercent,
		             p->volumes[v].center[0], p->volumes[v].center[1], p->volumes[v].center[2], p->volumes[v].half[0], p->volumes[v].half[1],
		             p->volumes[v].half[2]);
	}
	for (int t = 0; t < EXT_PHYS_TERRAINS; t++)
	{
		if (p->terrainSet[t] == 0)
		{
			continue;
		}
		Platform_Log("[CTR ExtPhys] terrain %s: speed %d accel %d counter_steer %d turn_lean %d friction %d turn_angle %d turn_response %d\n",
		             s_extPhysTerrainNames[t], s_extPhys.rows[t].speedMultiplier, s_extPhys.rows[t].slowUntilSpeed, s_extPhys.rows[t].counterSteerRatio,
		             s_extPhys.rows[t].turnLeanScale, s_extPhys.rows[t].groundFrictionScale, s_extPhys.rows[t].turnAngleScale,
		             s_extPhys.rows[t].turnResponseScale);
	}
}

// ---------------------------------------------------------------------------
// THE BOOST LOG (gate A3): a pad boost of a driver with the player physics
// (VehPhysForce_CollideDrivers) is noted at its first frame on the pad; the
// next EXT_PHYS_BOOST_WATCH frames follow the driver's speed, then one line:
//   [CTR ExtPhys] boost seat S frame F pad P pos X Y Z speed V0 -> peak V1 at +K frame(s), normal max VMAX
// F counts like the drive log's tick (game/DebugMenu.c): the tick that first
// shows the state - the drive log ticks before the game logic of a loop, the
// dispatcher after it, so an event in the logic of loop k is frame k + 1.
// P is the pad whose box (grown by EXT_PHYS_PAD_MARGIN) holds the kart, -1 none.
// VMAX = the seat's const_Speed_ClassStat (its top speed without a boost).

enum
{
	EXT_PHYS_SEATS = 8,
	EXT_PHYS_BOOST_WATCH = 15,
	EXT_PHYS_PAD_MARGIN = 64, // the kart's sweep sphere (radius 25, centre 25 up) reaches a pad 32 over the road from below
};

global_variable struct
{
	int tick;      // loops since the drive log's arming (completed dispatcher calls)
	int armed;
	int lastContact[EXT_PHYS_SEATS]; // the frame of the last pad contact (0 none)
	struct
	{
		int active;
		int frame;
		int pad;
		int pos[3];
		int v0;
		int peak;
		int peakAt;
		int aboveAt; // the first watched frame over the normal top speed, -1 none
		int left;
	} watch[EXT_PHYS_SEATS];
	int boosts;
	int boostsOffPad;
} s_extPhysRun;

void ExtPhys_NotePadBoost(const struct Driver *d)
{
	const struct GameTracker *gGT = sdata->gGT;
	int seat;
	int frame;

	if (!ExtTrack_Active() || (d == NULL) || (gGT == NULL) || !ExtTrack_ActiveForLevel(gGT->levelID) || !s_extPhysRun.armed)
	{
		return;
	}
	seat = d->driverID;
	if ((seat < 0) || (seat >= EXT_PHYS_SEATS))
	{
		return;
	}
	frame = s_extPhysRun.tick + 2;
	// one boost per stay on the pad: the frames on it follow each other
	if ((s_extPhysRun.lastContact[seat] != 0) && (s_extPhysRun.lastContact[seat] >= frame - 1))
	{
		s_extPhysRun.lastContact[seat] = frame;
		return;
	}
	s_extPhysRun.lastContact[seat] = frame;
	s_extPhysRun.watch[seat].active = 1;
	s_extPhysRun.watch[seat].frame = frame;
	s_extPhysRun.watch[seat].pos[0] = d->posCurr.x >> 8;
	s_extPhysRun.watch[seat].pos[1] = d->posCurr.y >> 8;
	s_extPhysRun.watch[seat].pos[2] = d->posCurr.z >> 8;
	s_extPhysRun.watch[seat].pad =
	    ExtColl_PadAt(s_extPhysRun.watch[seat].pos[0], s_extPhysRun.watch[seat].pos[1], s_extPhysRun.watch[seat].pos[2], EXT_PHYS_PAD_MARGIN);
	s_extPhysRun.watch[seat].v0 = d->speed;
	s_extPhysRun.watch[seat].peak = d->speed;
	s_extPhysRun.watch[seat].peakAt = -1;
	s_extPhysRun.watch[seat].aboveAt = -1;
	s_extPhysRun.watch[seat].left = EXT_PHYS_BOOST_WATCH + 1;
}

// ---------------------------------------------------------------------------
// THE ANTI-GRAVITY ZONES. A zone is a surface: the
// ground records of the collision nodes with "zone": n (physics.zones). A
// driver is in zone n from the frame it stands on such a record; it leaves when
// it stands on a record without a zone, or after air_hold_ms in the air. In the
// zone the gravity points against the zone's up (the surface normal, blended
// per frame), the ground ray runs along -up, the up the physics starts a frame
// with is the zone's up, and speed <-> velocity are taken in the surface frame
// (so steering turns about the normal). Nothing of this runs outside a zone;
// without zones in the track no driver is ever in one.

typedef struct
{
	int inZone;
	int zone;      // index into s_extPhys.p.zones + 1, 0 none
	SVec3 up;      // the zone's up, 4096 = 1.0
	int airMs;
	int leftFrame; // the frame the driver left a zone (for the "after zone" line), 0 none
	int leftZone;
	int logged;    // the "after zone" line is written
} ExtPhysSeatZone;

global_variable ExtPhysSeatZone s_extPhysZone[EXT_PHYS_SEATS];

// The zones are in force: the track lists zones, the collision has zone
// records, the track holds the running level.
internal int ExtPhys_ZonesInForce(void)
{
	if (s_extPhys.p.zoneCount == 0)
	{
		return 0;
	}
	return (ExtColl_ZoneRecords() > 0) && ExtPhys_InForce();
}

internal int ExtPhys_ZoneIndex(int id)
{
	for (int z = 0; z < s_extPhys.p.zoneCount; z++)
	{
		if (s_extPhys.p.zones[z].id == id)
		{
			return z;
		}
	}
	return -1;
}

internal ExtPhysSeatZone *ExtPhys_SeatZone(const struct Driver *d)
{
	if ((d == NULL) || (d->driverID < 0) || (d->driverID >= EXT_PHYS_SEATS) || !ExtPhys_ZonesInForce())
	{
		return NULL;
	}
	return &s_extPhysZone[d->driverID];
}

// v normalised to 4096; up (0, 4096, 0) for a zero vector.
internal SVec3 ExtPhys_Normalize(double x, double y, double z)
{
	const double len = sqrt(x * x + y * y + z * z);
	SVec3 out = {0, 4096, 0};

	if (len > 1e-6)
	{
		out.x = (s16)floor(4096.0 * x / len + 0.5);
		out.y = (s16)floor(4096.0 * y / len + 0.5);
		out.z = (s16)floor(4096.0 * z / len + 0.5);
	}
	return out;
}

// The blended up of a zone: (256 - blend) parts of the old one, blend of the
// surface normal, normalised; never further than maxUpDeg from world up
// (then turned back towards it on the great circle).
internal SVec3 ExtPhys_BlendUp(SVec3 old, SVec3 normal, int blend, int maxUpDeg)
{
	const double k = (double)blend / 256.0;
	double x = old.x * (1.0 - k) + normal.x * k;
	double y = old.y * (1.0 - k) + normal.y * k;
	double z = old.z * (1.0 - k) + normal.z * k;
	SVec3 up = ExtPhys_Normalize(x, y, z);
	const double maxRad = maxUpDeg * 3.14159265358979323846 / 180.0;
	const double ang = acos(fmax(-1.0, fmin(1.0, up.y / 4096.0)));

	if (ang > maxRad)
	{
		const double h = sqrt((double)up.x * up.x + (double)up.z * up.z);

		if (h > 1e-6)
		{
			up = ExtPhys_Normalize(sin(maxRad) * up.x / h, cos(maxRad), sin(maxRad) * up.z / h);
		}
	}
	return up;
}

// The angle between two unit vectors (4096), in degrees.
internal double ExtPhys_AngleDeg(SVec3 a, SVec3 b)
{
	// by the vectors' own lengths: the game's normals are not exactly 4096 long,
	// and acos is steep near 1 (4095.6 / 4096 alone would read as 1.2 degrees)
	const double la = sqrt((double)a.x * a.x + (double)a.y * a.y + (double)a.z * a.z);
	const double lb = sqrt((double)b.x * b.x + (double)b.y * b.y + (double)b.z * b.z);
	const double d = ((la > 0.0) && (lb > 0.0)) ? ((double)a.x * b.x + (double)a.y * b.y + (double)a.z * b.z) / (la * lb) : 1.0;

	return acos(fmax(-1.0, fmin(1.0, d))) * 180.0 / 3.14159265358979323846;
}

internal void ExtPhys_LeaveZone(int seat, ExtPhysSeatZone *s, const struct Driver *d, const char *why)
{
	const int frame = s_extPhysRun.tick + 2;

	Platform_Log("[CTR ExtZone] seat %d leave zone %d frame %d lap %d (%s)\n", seat, s_extPhys.p.zones[s->zone - 1].id, frame, (int)d->lapIndex, why);
	s->leftFrame = frame;
	s->leftZone = s_extPhys.p.zones[s->zone - 1].id;
	s->logged = 0;
	s->inZone = 0;
	s->zone = 0;
	s->airMs = 0;
	s->up = (SVec3){0, 4096, 0};
}

// The rule of a frame: grounded on a record of zone `id` (0 none), the normal
// of the ground, the frame's milliseconds.
internal void ExtPhys_ZoneStep(int seat, const struct Driver *d, int grounded, int id, SVec3 normal, int ms)
{
	ExtPhysSeatZone *s = &s_extPhysZone[seat];
	const int z = (id > 0) ? ExtPhys_ZoneIndex(id) : -1;

	if (grounded && (z >= 0))
	{
		if (!s->inZone || (s->zone != z + 1))
		{
			if (!s->inZone)
			{
				s->up = (SVec3){0, 4096, 0};
			}
			s->inZone = 1;
			s->zone = z + 1;
			s->leftFrame = 0;
			Platform_Log("[CTR ExtZone] seat %d enter zone %d frame %d lap %d\n", seat, id, s_extPhysRun.tick + 2, (int)d->lapIndex);
		}
		s->airMs = 0;
		s->up = ExtPhys_BlendUp(s->up, normal, s_extPhys.p.zones[z].upBlend, s_extPhys.p.zones[z].maxUpDeg);
		return;
	}
	if (!s->inZone)
	{
		return;
	}
	if (grounded)
	{
		ExtPhys_LeaveZone(seat, s, d, "ground without a zone");
		return;
	}
	s->airMs += ms;
	if (s->airMs > s_extPhys.p.zones[s->zone - 1].airHoldMs)
	{
		ExtPhys_LeaveZone(seat, s, d, "air hold over");
	}
}

// THE MAGGRAV SPLINES (night run 1, NL1_M1_SPEZ.md Z5-Z7, G1-G6, K4). A
// driver in a maggrav box follows the box's spline: on entering the nearest
// node over the whole spline, then every frame the nearest segment of the old
// node and its neighbours. In a zone the gravity then points along the
// spline's direction at the driver (PLANE: the nodes' directions blended,
// POINT: towards the spline, INVPOINT: away from it) - except on the ground
// under maglev_low_speed, where the zone's up keeps it (K4). Without a box,
// spline or low speed nothing of this runs.

typedef struct
{
	int box;        // the maggrav box the driver is in, + 1 (0 none)
	int spline;     // its spline + 1 (0 none or unusable)
	int node;       // the segment the driver is at, -1 none
	int fromSpline; // the last frame's gravity came from the spline (log only)
} ExtPhysMag;

global_variable ExtPhysMag s_extPhysMag[EXT_PHYS_SEATS];

internal const double *ExtPhys_Node(const ExtPhysParams *p, int spline, int i)
{
	return p->nodes[p->splines[spline].first + i];
}

// The squared distance of P from segment j -> j + 1 (0x28B3F0's arithmetic).
internal double ExtPhys_SegmentDist2(const ExtPhysParams *p, int spline, int j, const double P[3])
{
	const double *a = ExtPhys_Node(p, spline, j);
	const double *b = ExtPhys_Node(p, spline, j + 1);
	const double d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
	const double dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
	const double u = (dd > 0.0) ? (((P[0] - a[0]) * d[0] + (P[1] - a[1]) * d[1] + (P[2] - a[2]) * d[2]) / dd) : 0.0;
	double c[3];

	for (int k = 0; k < 3; k++)
	{
		c[k] = (u <= 0.0) ? a[k] : ((u >= 1.0) ? b[k] : (a[k] + u * d[k]));
	}
	return (c[0] - P[0]) * (c[0] - P[0]) + (c[1] - P[1]) * (c[1] - P[1]) + (c[2] - P[2]) * (c[2] - P[2]);
}

// Z7: segments max(i - 1, 0) .. min(i + 1, n - 2), the nearest wins, the first
// of equal ones; -1 for a spline without a segment.
internal int ExtPhys_SplineLocal(const ExtPhysParams *p, int spline, int i, const double P[3])
{
	const int n = p->splines[spline].count;
	const int lo = (i - 1 < 0) ? 0 : (i - 1);
	const int hi = (i + 1 > n - 2) ? (n - 2) : (i + 1);
	int best = -1;
	double bestD = 0.0;

	for (int j = lo; j <= hi; j++)
	{
		const double dist = ExtPhys_SegmentDist2(p, spline, j, P);

		if ((best < 0) || (dist < bestD))
		{
			best = j;
			bestD = dist;
		}
	}
	return best;
}

// Z6 (0x28B320): the nearest node over the whole spline, then Z7 from it.
internal int ExtPhys_SplineNearest(const ExtPhysParams *p, int spline, const double P[3])
{
	int best = -1;
	double bestD = 0.0;

	for (int i = 0; i < p->splines[spline].count; i++)
	{
		const double *a = ExtPhys_Node(p, spline, i);
		const double dist = (a[0] - P[0]) * (a[0] - P[0]) + (a[1] - P[1]) * (a[1] - P[1]) + (a[2] - P[2]) * (a[2] - P[2]);

		if ((best < 0) || (dist < bestD))
		{
			best = i;
			bestD = dist;
		}
	}
	return (best < 0) ? -1 : ExtPhys_SplineLocal(p, spline, best, P);
}

// G1-G6 (0x28B600): the direction of the gravity at P from segment i; 0 when
// it has no length (a point on the spline of type POINT / INVPOINT).
internal int ExtPhys_SplineDir(const ExtPhysParams *p, int spline, int i, const double P[3], double out[3])
{
	const int n = p->splines[spline].count;
	const int ia = (i >= n - 1) ? (n - 2) : i;
	const double *a = ExtPhys_Node(p, spline, ia);
	const double *b = ExtPhys_Node(p, spline, ia + 1);
	const double d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
	const double dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
	double u = (dd > 0.0) ? (((P[0] - a[0]) * d[0] + (P[1] - a[1]) * d[1] + (P[2] - a[2]) * d[2]) / dd) : 0.0;
	double v[3], len;

	u = (u <= 0.0) ? 0.0 : ((u >= 1.0) ? 1.0 : u);
	for (int k = 0; k < 3; k++)
	{
		const double c = a[k] + u * d[k];

		switch (p->splines[spline].type)
		{
		case EXT_PHYS_GRAV_PLANE:
			v[k] = a[3 + k] + u * (b[3 + k] - a[3 + k]);
			break;
		case EXT_PHYS_GRAV_POINT:
			v[k] = c - P[k];
			break;
		default: // INVPOINT (the reader keeps no other type)
			v[k] = P[k] - c;
			break;
		}
	}
	len = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (len < 1e-9)
	{
		return 0;
	}
	for (int k = 0; k < 3; k++)
	{
		out[k] = v[k] / len;
	}
	return 1;
}

// Z5-Z7: the maggrav box of the frame (after the zone rule; also outside a
// zone, as the source game keeps it) and the segment on its spline.
internal void ExtPhys_MaggravStep(int seat, const struct Driver *d)
{
	const ExtPhysParams *p = &s_extPhys.p;
	const double pos[3] = {d->posCurr.x / 256.0, d->posCurr.y / 256.0, d->posCurr.z / 256.0};
	ExtPhysMag *m = &s_extPhysMag[seat];

	if (p->maggravCount == 0)
	{
		return;
	}
	if ((m->box != 0) && !ExtPhys_InBox(&p->maggrav[m->box - 1].box, pos))
	{
		Platform_Log("[CTR ExtZone] seat %d leave maggrav box %s frame %d\n", seat, p->maggrav[m->box - 1].box.name, s_extPhysRun.tick + 2);
		m->box = 0;
		m->spline = 0;
		m->node = -1;
	}
	if (m->box == 0)
	{
		for (int v = 0; v < p->maggravCount; v++)
		{
			const int sp = p->maggrav[v].spline;

			if (!ExtPhys_InBox(&p->maggrav[v].box, pos))
			{
				continue;
			}
			m->box = v + 1;
			m->spline = ((sp < p->splineCount) && (p->splines[sp].count >= 2)) ? (sp + 1) : 0;
			m->node = (m->spline != 0) ? ExtPhys_SplineNearest(p, sp, pos) : -1;
			Platform_Log("[CTR ExtZone] seat %d maggrav box %s spline %d node %d frame %d\n", seat, p->maggrav[v].box.name, m->spline - 1, m->node,
			             s_extPhysRun.tick + 2);
			break;
		}
	}
	if (m->spline != 0)
	{
		m->node = ExtPhys_SplineLocal(p, m->spline - 1, m->node, pos);
	}
}

// K4/K5: the gravity's direction (4096) from the driver's spline; 0 when the
// zone's up keeps it: no spline (or no low speed read), or on the ground
// slower than maglev_low_speed (the speed in the plane of the zone's up,
// NL1_M1_SPEZ.md A4).
internal int ExtPhys_SplineGravityWith(const ExtPhysParams *p, const ExtPhysMag *m, const struct Driver *d, SVec3 up, s16 dir[3])
{
	double pos[3], out[3], v[3], vn, speed2;

	if (!p->lowSpeedSet || (m->spline == 0) || (m->node < 0))
	{
		return 0;
	}
	v[0] = d->velocity.x;
	v[1] = d->velocity.y;
	v[2] = d->velocity.z;
	vn = (v[0] * up.x + v[1] * up.y + v[2] * up.z) / 4096.0;
	v[0] -= vn * up.x / 4096.0;
	v[1] -= vn * up.y / 4096.0;
	v[2] -= vn * up.z / 4096.0;
	speed2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	if (((d->actionsFlagSet & ACTION_TOUCH_GROUND) != 0) && (speed2 < (double)p->lowSpeed * (double)p->lowSpeed))
	{
		return 0;
	}
	pos[0] = d->posCurr.x / 256.0;
	pos[1] = d->posCurr.y / 256.0;
	pos[2] = d->posCurr.z / 256.0;
	if (!ExtPhys_SplineDir(p, m->spline - 1, m->node, pos, out))
	{
		return 0;
	}
	for (int k = 0; k < 3; k++)
	{
		dir[k] = (s16)floor(4096.0 * out[k] + 0.5);
	}
	return 1;
}

// THE MAGLEV BOXES (night run 1, NL1_M1_SPEZ.md Z2). The source game switches
// its anti-gravity state also by boxes: a driver outside a zone that is in a
// "maglev-on" box enters the track's first zone (its up stays world up until
// it stands on the zone's surface), one in a zone that is in a "maglev-off"
// box leaves it. The surface the driver stands on this frame has the last
// word, as the source game's contacts have it after its box test (Z9): an
// "on" box over a road without a zone gives no zone, an "off" box over the
// zone's own surface does not take the driver out. Only the player physics
// (ExtPhys_AfterGroundSearch); without boxes nothing of this runs.
// grounded / zoneGround: the driver stands on a record / on one of a zone.
internal void ExtPhys_MaglevBoxes(int seat, const struct Driver *d, int grounded, int zoneGround)
{
	ExtPhysSeatZone *s = &s_extPhysZone[seat];
	const double pos[3] = {d->posCurr.x / 256.0, d->posCurr.y / 256.0, d->posCurr.z / 256.0};

	if ((s_extPhys.p.maglevCount == 0) || (s_extPhys.p.zoneCount == 0) || (s->inZone ? zoneGround : (grounded && !zoneGround)))
	{
		return;
	}
	for (int v = 0; v < s_extPhys.p.maglevCount; v++)
	{
		if ((s_extPhys.p.maglev[v].on != (s->inZone ? 0 : 1)) || !ExtPhys_InBox(&s_extPhys.p.maglev[v].box, pos))
		{
			continue;
		}
		Platform_Log("[CTR ExtZone] seat %d maglev box %s (%s) frame %d\n", seat, s_extPhys.p.maglev[v].box.name, s_extPhys.p.maglev[v].on ? "on" : "off",
		             s_extPhysRun.tick + 2);
		if (s->inZone)
		{
			ExtPhys_LeaveZone(seat, s, d, "maglev-off box");
			return;
		}
		s->inZone = 1;
		s->zone = 1;
		s->up = (SVec3){0, 4096, 0};
		s->airMs = 0;
		s->leftFrame = 0;
		Platform_Log("[CTR ExtZone] seat %d enter zone %d frame %d lap %d\n", seat, s_extPhys.p.zones[0].id, s_extPhysRun.tick + 2, (int)d->lapIndex);
		return;
	}
}

// Z-a: after the ground search of a driver with the player physics
// (COLL_FIXED_PlayerSearch): its ground record decides the zone (before it
// the maglev boxes of the track, if any).
void ExtPhys_AfterGroundSearch(const struct Driver *d)
{
	const int grounded = ((d->collisionFlags & DRIVER_COLL_FLAG_GROUNDED) != 0) && (d->currBlockTouching != NULL);
	int id;

	if (ExtPhys_SeatZone(d) == NULL)
	{
		return;
	}
	id = grounded ? ExtColl_RecordZone(d->currBlockTouching) : 0;
	ExtPhys_MaglevBoxes(d->driverID, d, grounded, (id > 0) && (ExtPhys_ZoneIndex(id) >= 0));
	ExtPhys_ZoneStep(d->driverID, d, grounded, id, d->normalVecUP, sdata->gGT->elapsedTimeMS);
	ExtPhys_MaggravStep(d->driverID, d);
}

void ExtPhys_BotGround(const struct Driver *bot, const struct QuadBlock *ground, const SVec3 *normal)
{
	if ((ExtPhys_SeatZone(bot) == NULL) || (normal == NULL))
	{
		return;
	}
	ExtPhys_ZoneStep(bot->driverID, bot, ground != NULL, (ground != NULL) ? ExtColl_RecordZone(ground) : 0, *normal, sdata->gGT->elapsedTimeMS);
}

void ExtPhys_SeedDriver(struct Driver *d, const SVec3 *up, int zone)
{
	ExtPhysSeatZone *s = ExtPhys_SeatZone(d);
	const int z = (zone > 0) ? ExtPhys_ZoneIndex(zone) : -1;

	if (s == NULL)
	{
		return;
	}
	// a set-down (respawn, start) chooses the maggrav box and its node anew
	memset(&s_extPhysMag[d->driverID], 0, sizeof(s_extPhysMag[d->driverID]));
	s_extPhysMag[d->driverID].node = -1;
	if ((z < 0) || (up == NULL))
	{
		memset(s, 0, sizeof(*s));
		s->up = (SVec3){0, 4096, 0};
		return;
	}
	s->inZone = 1;
	s->zone = z + 1;
	s->airMs = 0;
	s->leftFrame = 0;
	s->up = ExtPhys_Normalize(up->x, up->y, up->z);
	d->normalVecUP = s->up;
	d->AxisAngle1_normalVec = s->up;
	d->AxisAngle2_normalVec = s->up;
	d->AxisAngle3_normalVec = s->up;
	d->AxisAngle4_normalVec = s->up;
	Platform_Log("[CTR ExtZone] seat %d seeded in zone %d frame %d up %d %d %d\n", d->driverID, zone, s_extPhysRun.tick + 2, s->up.x, s->up.y, s->up.z);
}

int ExtPhys_DriverZone(const struct Driver *d)
{
	const ExtPhysSeatZone *s = ExtPhys_SeatZone(d);

	return ((s != NULL) && s->inZone) ? s_extPhys.p.zones[s->zone - 1].id : 0;
}

int ExtPhys_DriverUp(const struct Driver *d, SVec3 *up)
{
	const ExtPhysSeatZone *s = ExtPhys_SeatZone(d);

	if ((s == NULL) || !s->inZone)
	{
		*up = (SVec3){0, 4096, 0};
		return 0;
	}
	*up = s->up;
	return 1;
}

int ExtPhys_DriverForward(const struct Driver *d, SVec3 *fwd)
{
	fwd->x = d->matrixFacingDir.m[0][2];
	fwd->y = d->matrixFacingDir.m[1][2];
	fwd->z = d->matrixFacingDir.m[2][2];
	return ExtPhys_DriverZone(d) != 0;
}

// Z-b: the frame's gravity (already times the frame time, negative = down) as
// a world vector against the zone's up, times the zone's share. 0 outside.
int ExtPhys_ZoneGravity(const struct Driver *d, int gravity, s16 out[3])
{
	const ExtPhysSeatZone *s = ExtPhys_SeatZone(d);
	int g;

	if ((s == NULL) || !s->inZone)
	{
		return 0;
	}
	g = CTR_MipsDiv(CTR_MipsMulLo(gravity, s_extPhys.p.zones[s->zone - 1].gravityPercent), EXT_PHYS_GRAVITY_NEUTRAL);
	{
		// the maggrav spline's direction, where the track gives one (A3): the
		// same share, along the spline's "down" instead of against the zone's up
		ExtPhysMag *m = &s_extPhysMag[d->driverID];
		s16 dir[3];
		const int fromSpline = ExtPhys_SplineGravityWith(&s_extPhys.p, m, d, s->up, dir);

		if (fromSpline != m->fromSpline)
		{
			if (fromSpline)
			{
				Platform_Log("[CTR ExtZone] seat %d gravity from spline %d node %d dir %d %d %d frame %d\n", d->driverID, m->spline - 1, m->node, dir[0], dir[1],
				             dir[2], s_extPhysRun.tick + 2);
			}
			else
			{
				Platform_Log("[CTR ExtZone] seat %d gravity from zone up frame %d\n", d->driverID, s_extPhysRun.tick + 2);
			}
			m->fromSpline = fromSpline;
		}
		if (fromSpline)
		{
			out[0] = (s16)((-g * dir[0]) >> 12);
			out[1] = (s16)((-g * dir[1]) >> 12);
			out[2] = (s16)((-g * dir[2]) >> 12);
			return 1;
		}
	}
	out[0] = (s16)((g * s->up.x) >> 12);
	out[1] = (s16)((g * s->up.y) >> 12);
	out[2] = (s16)((g * s->up.z) >> 12);
	return 1;
}

// Z-e: the surface frame of a normal n (4096): right r, n, forward f at
// heading 0 - the columns VehPhysForce_RotAxisAngle builds for angle 0 (the
// same heading convention as the kart's own frame), in plain integers.
internal void ExtPhys_SurfaceFrame(SVec3 n, s32 r[3], s32 f[3])
{
	const s32 nx = n.x, ny = n.y, nz = n.z;
	const s32 denom = nx * nx + nz * nz;

	if (denom == 0)
	{
		f[0] = 0;
		f[1] = 0;
		f[2] = ny;
	}
	else
	{
		const double cosRem = 4096.0 - ny;

		f[0] = (s32)floor(cosRem * (double)(-nx * nz) / (double)denom + 0.5);
		f[1] = -nz;
		f[2] = ny + (s32)floor(cosRem * (double)(nx * nx) / (double)denom + 0.5);
	}
	r[0] = (ny * f[2] - nz * f[1]) >> 12;
	r[1] = (nz * f[0] - nx * f[2]) >> 12;
	r[2] = (nx * f[1] - ny * f[0]) >> 12;
}

int ExtPhys_ZoneLocalToWorld(const struct Driver *d, const s32 local[3], Vec3 *world)
{
	s32 r[3], f[3];
	const SVec3 n = d->AxisAngle1_normalVec;

	if (ExtPhys_DriverZone(d) == 0)
	{
		return 0;
	}
	ExtPhys_SurfaceFrame(n, r, f);
	world->x = (s32)(((s64)r[0] * local[0] + (s64)n.x * local[1] + (s64)f[0] * local[2]) >> 12);
	world->y = (s32)(((s64)r[1] * local[0] + (s64)n.y * local[1] + (s64)f[1] * local[2]) >> 12);
	world->z = (s32)(((s64)r[2] * local[0] + (s64)n.z * local[1] + (s64)f[2] * local[2]) >> 12);
	return 1;
}

int ExtPhys_ZoneWorldToLocal(const struct Driver *d, const Vec3 *world, s32 local[3])
{
	s32 r[3], f[3];
	const SVec3 n = d->AxisAngle1_normalVec;

	if (ExtPhys_DriverZone(d) == 0)
	{
		return 0;
	}
	ExtPhys_SurfaceFrame(n, r, f);
	local[0] = (s32)(((s64)r[0] * world->x + (s64)r[1] * world->y + (s64)r[2] * world->z) >> 12);
	local[1] = (s32)(((s64)n.x * world->x + (s64)n.y * world->y + (s64)n.z * world->z) >> 12);
	local[2] = (s32)(((s64)f[0] * world->x + (s64)f[1] * world->y + (s64)f[2] * world->z) >> 12);
	return 1;
}

int ExtPhys_ZoneHeadingTo(const struct Driver *d, int dx, int dy, int dz, int *heading)
{
	const Vec3 world = {dx, dy, dz};
	s32 local[3];

	if (!ExtPhys_ZoneWorldToLocal(d, &world, local))
	{
		return 0;
	}
	*heading = ratan2(local[0], local[2]) & 0xfff;
	return 1;
}

int ExtPhys_GroundProbe(const struct Driver *d, const s32 pos[3], const SVec3 *upHint, ExtCollHit *hit, SVec3 *upUsed)
{
	SVec3 up = {0, 4096, 0};
	SVec3 from, dir;
	s32 px, py, pz;

	if ((d == NULL) || (pos == NULL) || (hit == NULL))
	{
		return 0;
	}
	if (!ExtPhys_DriverUp(d, &up) && (upHint != NULL))
	{
		up = *upHint;
	}
	if (upUsed != NULL)
	{
		*upUsed = up;
	}
	px = CTR_MipsSra(pos[0], 8);
	py = CTR_MipsSra(pos[1], 8);
	pz = CTR_MipsSra(pos[2], 8);
	// from EXT_PHYS_PROBE_TOP over the driver along up, EXT_PHYS_PROBE_TOP +
	// EXT_PHYS_PROBE_BOTTOM long against up: the player's window (COLL.c,
	// COLL_FIXED_PlayerSearch_SetupSearch)
	from.x = (s16)(px + ((up.x * EXT_PHYS_PROBE_TOP) >> 12));
	from.y = (s16)(py + ((up.y * EXT_PHYS_PROBE_TOP) >> 12));
	from.z = (s16)(pz + ((up.z * EXT_PHYS_PROBE_TOP) >> 12));
	dir.x = (s16)-up.x;
	dir.y = (s16)-up.y;
	dir.z = (s16)-up.z;
	return ExtColl_ProbeSurface(&from, &dir, EXT_PHYS_PROBE_TOP + EXT_PHYS_PROBE_BOTTOM, hit);
}

int ExtPhys_BotGroundAt(const struct Driver *d, const s32 pos[3], const SVec3 *upHint, int below, int above, int behindMax, ExtCollHit *hit, SVec3 *upUsed,
                        int *fallback)
{
	SVec3 up = {0, 4096, 0};
	double from[3], dir[3];
	ExtCollLineHit lh;

	if (fallback != NULL)
	{
		*fallback = 0;
	}
	if ((d == NULL) || (pos == NULL) || (hit == NULL))
	{
		return 0;
	}
	if (ExtPhys_GroundProbe(d, pos, upHint, hit, &up))
	{
		// its height over the kart along up: a face above the step is no floor
		const s32 over = (s32)((((s64)(hit->pos.x - CTR_MipsSra(pos[0], 8)) * up.x) + ((s64)(hit->pos.y - CTR_MipsSra(pos[1], 8)) * up.y) +
		                        ((s64)(hit->pos.z - CTR_MipsSra(pos[2], 8)) * up.z)) >> 12);

		if (upUsed != NULL)
		{
			*upUsed = up;
		}
		if (over <= above)
		{
			return 1;
		}
	}
	else if (upUsed != NULL)
	{
		*upUsed = up;
	}
	from[0] = pos[0] / 256.0;
	from[1] = pos[1] / 256.0;
	from[2] = pos[2] / 256.0;
	dir[0] = up.x;
	dir[1] = up.y;
	dir[2] = up.z;
	if (!ExtColl_LineQuery(from, dir, -(double)below, (double)above, EXT_COLL_LINE_GROUND, 1, &lh))
	{
		return 0;
	}
	{
		const double side = ((from[0] - lh.pos[0]) * lh.normal[0]) + ((from[1] - lh.pos[1]) * lh.normal[1]) + ((from[2] - lh.pos[2]) * lh.normal[2]);

		if (side < -(double)behindMax)
		{
			return 0;
		}
	}
	hit->pos.x = (s16)floor(lh.pos[0] + 0.5);
	hit->pos.y = (s16)floor(lh.pos[1] + 0.5);
	hit->pos.z = (s16)floor(lh.pos[2] + 0.5);
	hit->normal.x = (s16)floor((lh.normal[0] * 4096.0) + 0.5);
	hit->normal.y = (s16)floor((lh.normal[1] * 4096.0) + 0.5);
	hit->normal.z = (s16)floor((lh.normal[2] * 4096.0) + 0.5);
	hit->record = lh.record;
	hit->zone = lh.zone;
	hit->terrain = lh.terrain;
	if (fallback != NULL)
	{
		*fallback = 1;
	}
	return 1;
}

// ---------------------------------------------------------------------------
// GRAVITY VOLUMES (physics.gravity_volumes): boxes in which the gravity has its
// own share (a track's low-gravity jumps, a tube that presses harder). The box
// that holds the kart's position scales the frame's gravity after the track's
// own share; the first box in the list wins.

global_variable int s_extPhysVolumeSeat[EXT_PHYS_SEATS]; // the box a seat is in, + 1 (0 none)

// The box that holds point p, -1 none.
internal int ExtPhys_VolumeAtWith(const ExtPhysParams *p, const double pos[3])
{
	for (int v = 0; v < p->volumeCount; v++)
	{
		const double d[3] = {pos[0] - p->volumes[v].center[0], pos[1] - p->volumes[v].center[1], pos[2] - p->volumes[v].center[2]};
		int inside = 1;

		for (int a = 0; inside && (a < 3); a++)
		{
			const double along = d[0] * p->volumes[v].axis[a][0] + d[1] * p->volumes[v].axis[a][1] + d[2] * p->volumes[v].axis[a][2];

			inside = fabs(along) <= p->volumes[v].half[a];
		}
		if (inside)
		{
			return v;
		}
	}
	return -1;
}

int ExtPhys_ScaleGravityFor(const struct Driver *d, int gravity)
{
	int v;
	int seat;
	double pos[3];

	gravity = ExtPhys_ScaleGravity(gravity);
	// without volumes (and always without --ext-track) nothing further runs
	if ((s_extPhys.p.volumeCount == 0) || (d == NULL) || !ExtPhys_InForce())
	{
		return gravity;
	}
	pos[0] = (double)(d->posCurr.x >> 8);
	pos[1] = (double)(d->posCurr.y >> 8);
	pos[2] = (double)(d->posCurr.z >> 8);
	v = ExtPhys_VolumeAtWith(&s_extPhys.p, pos);
	seat = d->driverID;
	if ((seat >= 0) && (seat < EXT_PHYS_SEATS) && (s_extPhysVolumeSeat[seat] != v + 1))
	{
		if (s_extPhysVolumeSeat[seat] != 0)
		{
			Platform_Log("[CTR ExtPhys] seat %d leave gravity volume %d (%s) frame %d\n", seat, s_extPhysVolumeSeat[seat] - 1,
			             s_extPhys.p.volumes[s_extPhysVolumeSeat[seat] - 1].name, s_extPhysRun.tick + 2);
		}
		if (v >= 0)
		{
			Platform_Log("[CTR ExtPhys] seat %d enter gravity volume %d (%s, %d%%) frame %d pos %d %d %d\n", seat, v, s_extPhys.p.volumes[v].name,
			             s_extPhys.p.volumes[v].gravityPercent, s_extPhysRun.tick + 2, (int)pos[0], (int)pos[1], (int)pos[2]);
		}
		s_extPhysVolumeSeat[seat] = v + 1;
	}
	if (v < 0)
	{
		return gravity;
	}
	return CTR_MipsDiv(CTR_MipsMulLo(gravity, s_extPhys.p.volumes[v].gravityPercent), EXT_PHYS_GRAVITY_NEUTRAL);
}

// Z-h: the zone lines of a frame (after the game logic).
internal void ExtPhys_ZoneFrameLog(const struct GameTracker *gGT)
{
	if (!ExtPhys_ZonesInForce())
	{
		return;
	}
	for (int seat = 0; seat < EXT_PHYS_SEATS; seat++)
	{
		const struct Driver *d = gGT->drivers[seat];
		ExtPhysSeatZone *s = &s_extPhysZone[seat];
		const int frame = s_extPhysRun.tick + 1;
		const int ground = (d != NULL) && ((d->actionsFlagSet & ACTION_TOUCH_GROUND) != 0);

		if (d == NULL)
		{
			continue;
		}
		if (s->inZone)
		{
			// tilt: the kart's up (its drawn frame) against the ground normal
			// (in the air: against the zone's up)
			const double tilt = ExtPhys_AngleDeg(d->AxisAngle2_normalVec, ground ? d->normalVecUP : s->up);

			Platform_Log("[CTR ExtZone] seat %d frame %d zone %d ground %d tilt %.1f up %d %d %d pos %d %d %d\n", seat, frame,
			             s_extPhys.p.zones[s->zone - 1].id, ground, tilt, s->up.x, s->up.y, s->up.z, d->posCurr.x >> 8, d->posCurr.y >> 8,
			             d->posCurr.z >> 8);
		}
		else if ((s->leftFrame != 0) && !s->logged && ground)
		{
			Platform_Log("[CTR ExtZone] seat %d after zone %d: gravity 0 -4096 0, ground at +%d frame(s)\n", seat, s->leftZone, frame - s->leftFrame);
			s->logged = 1;
		}
	}
}

void ExtPhys_OnLogicFrame(void)
{
	const struct GameTracker *gGT = sdata->gGT;

	// the drive log's arming (DebugMenu_DriveLogTick): not in a menu, not loading
	if ((gGT == NULL) || ((gGT->gameMode1 & (MAIN_MENU | LOADING)) != 0) || (sdata->Loading.stage != LOAD_IDLE))
	{
		memset(&s_extPhysRun, 0, sizeof(s_extPhysRun));
		memset(s_extPhysZone, 0, sizeof(s_extPhysZone));
		memset(s_extPhysVolumeSeat, 0, sizeof(s_extPhysVolumeSeat));
		memset(s_extPhysMag, 0, sizeof(s_extPhysMag));
		return;
	}
	s_extPhysRun.armed = 1;
	s_extPhysRun.tick++;
	ExtPhys_ZoneFrameLog(gGT);
	for (int seat = 0; seat < EXT_PHYS_SEATS; seat++)
	{
		const struct Driver *d = gGT->drivers[seat];

		if (!s_extPhysRun.watch[seat].active)
		{
			continue;
		}
		if (d == NULL)
		{
			s_extPhysRun.watch[seat].active = 0;
			continue;
		}
		// after this loop's logic: frame tick + 1, K frames after the boost
		if ((s_extPhysRun.watch[seat].peakAt < 0) || (d->speed > s_extPhysRun.watch[seat].peak))
		{
			s_extPhysRun.watch[seat].peak = d->speed;
			s_extPhysRun.watch[seat].peakAt = s_extPhysRun.tick + 1 - s_extPhysRun.watch[seat].frame;
		}
		if ((s_extPhysRun.watch[seat].aboveAt < 0) && (d->speed > d->const_Speed_ClassStat))
		{
			s_extPhysRun.watch[seat].aboveAt = s_extPhysRun.tick + 1 - s_extPhysRun.watch[seat].frame;
		}
		if (--s_extPhysRun.watch[seat].left > 0)
		{
			continue;
		}
		s_extPhysRun.watch[seat].active = 0;
		s_extPhysRun.boosts++;
		s_extPhysRun.boostsOffPad += (s_extPhysRun.watch[seat].pad < 0) ? 1 : 0;
		// "first above max at +J": the first of the watched frames whose speed is
		// over the normal top speed (-1: none of them)
		Platform_Log("[CTR ExtPhys] boost seat %d frame %d pad %d pos %d %d %d speed %d -> peak %d at +%d frame(s), normal max %d, first above max at +%d\n",
		             seat, s_extPhysRun.watch[seat].frame, s_extPhysRun.watch[seat].pad, s_extPhysRun.watch[seat].pos[0], s_extPhysRun.watch[seat].pos[1],
		             s_extPhysRun.watch[seat].pos[2], s_extPhysRun.watch[seat].v0, s_extPhysRun.watch[seat].peak, s_extPhysRun.watch[seat].peakAt,
		             d->const_Speed_ClassStat, s_extPhysRun.watch[seat].aboveAt);
	}
}

// ---------------------------------------------------------------------------
// --ext-selftest phys: the reader and the hooks on made-up sections, no window,
// no game data, no track

internal void ExtPhys_TestCheck(int ok, const char *what, int *n, int *failures)
{
	(*n)++;
	if (!ok)
	{
		(*failures)++;
		printf("ext selftest phys: FAILED %s\n", what);
	}
}

// Parses text and reads its "physics" (or the whole text when it has none).
internal int ExtPhys_TestRead(const char *text, ExtPhysParams *p)
{
	ExtJsonDoc doc;
	char why[128];

	memset(&doc, 0, sizeof(doc));
	if (!ExtJson_Parse(&doc, text, strlen(text), why, (int)sizeof(why)))
	{
		printf("ext selftest phys: made-up text does not parse: %s\n", why);
		return 0;
	}
	ExtPhys_Read(&doc, ExtJson_Get(&doc, 0, "physics"), p);
	ExtJson_Free(&doc);
	return 1;
}

int ExtPhys_Selftest(void)
{
	int checks = 0;
	int failures = 0;
	char what[256];
	char neutral[8192];
	size_t used = 0;
	ExtPhysParams absent, empty, neut, set;
	struct Terrain rows[EXT_PHYS_TERRAINS];

	// 1. missing section, empty section, a section of only neutral values
	ExtPhys_TestCheck(ExtPhys_TestRead("{\"collision\": {\"glb\": \"collision.glb\"}}", &absent) && !absent.present && !absent.anySet,
	                  "no section: nothing present, nothing set", &checks, &failures);
	ExtPhys_TestCheck(ExtPhys_TestRead("{\"physics\": {}}", &empty) && empty.present && !empty.anySet, "an empty section: present, nothing set", &checks,
	                  &failures);
	used += (size_t)snprintf(&neutral[used], sizeof(neutral) - used, "{\"physics\": {\"version\": 1, \"gravity_percent\": 100, "
	                                                                 "\"turbo_pad\": {\"reserves\": 960, \"fire_level\": 256}, \"terrain\": {");
	for (int t = 0; t < EXT_PHYS_TERRAINS; t++)
	{
		used += (size_t)snprintf(&neutral[used], sizeof(neutral) - used, "%s\"%s\": {", (t > 0) ? ", " : "", s_extPhysTerrainNames[t]);
		for (int f = 0; f < EXT_PHYS_TF_COUNT; f++)
		{
			used += (size_t)snprintf(&neutral[used], sizeof(neutral) - used, "%s\"%s\": %d", (f > 0) ? ", " : "", s_extPhysFieldNames[f],
			                         *ExtPhys_FieldOf(&data.MetaDataTerrain[t], f));
		}
		used += (size_t)snprintf(&neutral[used], sizeof(neutral) - used, "}");
	}
	snprintf(&neutral[used], sizeof(neutral) - used, "}}}");
	ExtPhys_TestCheck(used < sizeof(neutral) - 8, "the neutral text fits its buffer", &checks, &failures);
	ExtPhys_TestCheck(ExtPhys_TestRead(neutral, &neut), "the neutral section parses", &checks, &failures);
	snprintf(what, sizeof(what), "every value of the neutral section is kept as not set (%d neutral values, %d unknown, %d bad)", neut.neutralGiven,
	         neut.unknownKeys, neut.badValues);
	ExtPhys_TestCheck(neut.present && !neut.anySet && (neut.neutralGiven == 3 + EXT_PHYS_TERRAINS * EXT_PHYS_TF_COUNT) && (neut.unknownKeys == 0) &&
	                      (neut.badValues == 0),
	                  what, &checks, &failures);
	// the stored set of a neutral section is the one of a missing section
	// (only the flag "the section is there" and the reader's counters differ)
	neut.present = 0;
	neut.neutralGiven = 0;
	ExtPhys_TestCheck(memcmp(&neut, &absent, sizeof(neut)) == 0, "NEUTRAL = NOT SET: the neutral section stores what a missing section stores", &checks,
	                  &failures);

	// 2. the hooks hand their input back, untouched, for a missing and for a
	// neutral section - every gravity of the game's range, every terrain row,
	// the pad values
	{
		int same = 1;

		ExtPhys_MakeRows(&absent, rows);
		for (int g = -0x8000; g <= 0x7fff; g++)
		{
			if ((ExtPhys_ScaleGravityWith(&absent, g) != g) || (ExtPhys_ScaleGravityWith(&neut, g) != g))
			{
				same = 0;
			}
		}
		for (int t = 0; t < 256; t++)
		{
			struct Terrain *row = (t < EXT_PHYS_TERRAINS) ? &data.MetaDataTerrain[t] : &data.MetaDataTerrain[0];

			if ((ExtPhys_TerrainWith(&absent, rows, (unsigned char)t, row) != row) || (ExtPhys_TerrainWith(&neut, rows, (unsigned char)t, row) != row))
			{
				same = 0;
			}
		}
		ExtPhys_TestCheck(same, "missing / neutral: gravity -0x8000..0x7fff and every terrain row come back as given", &checks, &failures);
	}
	{
		// the public hooks without --ext-track (nothing read): input back
		int same = 1;

		memset(&s_extPhys, 0, sizeof(s_extPhys));
		for (int g = -0x8000; g <= 0x7fff; g += 7)
		{
			if (ExtPhys_ScaleGravity(g) != g)
			{
				same = 0;
			}
		}
		for (int t = 0; t < EXT_PHYS_TERRAINS; t++)
		{
			if (ExtPhys_Terrain((unsigned char)t, &data.MetaDataTerrain[t]) != &data.MetaDataTerrain[t])
			{
				same = 0;
			}
		}
		same = same && (ExtPhys_PadReserves(EXT_PHYS_PAD_RESERVES_NEUTRAL) == EXT_PHYS_PAD_RESERVES_NEUTRAL) &&
		       (ExtPhys_PadFireLevel(EXT_PHYS_PAD_FIRE_NEUTRAL) == EXT_PHYS_PAD_FIRE_NEUTRAL) && (ExtPhys_PadReserves(123) == 123);
		ExtPhys_TestCheck(same, "without --ext-track the public hooks hand every input back", &checks, &failures);
	}

	// 3. own values: gravity 80 %, one terrain field, pad values; clamping,
	// unknown keys, bad values
	ExtPhys_TestRead("{\"physics\": {\"gravity_percent\": 80, \"terrain\": {\"HARDPACK\": {\"speed\": 200, \"friction\": 256}, \"MAGIC\": {}}, "
	                 "\"turbo_pad\": {\"reserves\": 1500, \"fire_level\": 256}, \"wind\": 3}}",
	                 &set);
	ExtPhys_TestCheck(set.anySet && set.gravitySet && (set.gravityPercent == 80), "gravity_percent 80 is set", &checks, &failures);
	ExtPhys_TestCheck((ExtPhys_ScaleGravityWith(&set, -900) == -720) && (ExtPhys_ScaleGravityWith(&set, 0) == 0) &&
	                      (ExtPhys_ScaleGravityWith(&set, -901) == CTR_MipsDiv(-901 * 80, 100)),
	                  "80 %: -900 -> -720, the MODS arithmetic", &checks, &failures);
	{
		const int hp = TERRAIN_HARDPACK;
		const int tableFriction = data.MetaDataTerrain[hp].groundFrictionScale;
		struct Terrain *got;

		ExtPhys_MakeRows(&set, rows);
		got = ExtPhys_TerrainWith(&set, rows, (unsigned char)hp, &data.MetaDataTerrain[hp]);
		snprintf(what, sizeof(what), "HARDPACK speed 200 in a copy, the other fields the game's (friction %d %s)", tableFriction,
		         (tableFriction == 256) ? "neutral, not set" : "set");
		ExtPhys_TestCheck((got == &rows[hp]) && (got->speedMultiplier == 200) && (got->slowUntilSpeed == data.MetaDataTerrain[hp].slowUntilSpeed) &&
		                      (got->flags == data.MetaDataTerrain[hp].flags) && (got->sound == data.MetaDataTerrain[hp].sound) &&
		                      (got->groundFrictionScale == 256) && (data.MetaDataTerrain[hp].speedMultiplier != 200),
		                  what, &checks, &failures);
		ExtPhys_TestCheck(ExtPhys_TerrainWith(&set, rows, TERRAIN_ASPHALT, &data.MetaDataTerrain[TERRAIN_ASPHALT]) == &data.MetaDataTerrain[TERRAIN_ASPHALT],
		                  "a row without own values stays the game's row", &checks, &failures);
		ExtPhys_TestCheck(ExtPhys_TerrainWith(&set, rows, 200, &data.MetaDataTerrain[0]) == &data.MetaDataTerrain[0], "a type out of range keeps row 0",
		                  &checks, &failures);
	}
	ExtPhys_TestCheck(set.padReservesSet && (set.padReserves == 1500) && !set.padFireSet, "turbo pad reserves 1500 set, fire level 256 neutral", &checks,
	                  &failures);
	snprintf(what, sizeof(what), "unknown keys counted: the terrain MAGIC and 'wind' (%d)", set.unknownKeys);
	ExtPhys_TestCheck(set.unknownKeys == 2, what, &checks, &failures);
	{
		ExtPhysParams c;

		ExtPhys_TestRead("{\"physics\": {\"gravity_percent\": 5}}", &c);
		ExtPhys_TestCheck(c.gravitySet && (c.gravityPercent == EXT_PHYS_GRAVITY_MIN) && (c.clamped == 1), "gravity 5 is clamped to 25", &checks, &failures);
		ExtPhys_TestRead("{\"physics\": {\"gravity_percent\": 1000}}", &c);
		ExtPhys_TestCheck(c.gravitySet && (c.gravityPercent == EXT_PHYS_GRAVITY_MAX) && (c.clamped == 1), "gravity 1000 is clamped to 400", &checks, &failures);
		ExtPhys_TestRead("{\"physics\": {\"gravity_percent\": \"low\"}}", &c);
		ExtPhys_TestCheck(!c.anySet && (c.badValues == 1), "a gravity that is no number is not set and counted", &checks, &failures);
		ExtPhys_TestRead("{\"physics\": {\"version\": 2}}", &c);
		ExtPhys_TestCheck(!c.anySet && (c.badValues == 1), "an unknown version is counted", &checks, &failures);
		ExtPhys_TestRead("{\"physics\": [1, 2]}", &c);
		ExtPhys_TestCheck(!c.present && !c.anySet, "a section that is no object counts as missing", &checks, &failures);
	}

	// 4a. the surface frame against the game's own kart frame: the forward of
	// VehPhysForce_RotAxisAngle(n, a) must have heading a in ExtPhys_SurfaceFrame(n)
	{
		const SVec3 ns[5] = {{0, 4096, 0}, {0, 2896, -2896}, {0, 0, 4096}, {-18, -344, 4082}, {75, -492, 4066}};
		int worst = 0;

		for (int i = 0; i < 5; i++)
		{
			for (int a = 0; a < 4096; a += 256)
			{
				MATRIX m;
				SVec3 n = ns[i];
				s32 r[3], f[3];
				s32 lx, lz;
				int h, e;

				VehPhysForce_RotAxisAngle(&m, CTR_VECTOR_DATA(&n), (s16)a);
				ExtPhys_SurfaceFrame(ns[i], r, f);
				lx = (s32)(((s64)r[0] * m.m[0][2] + (s64)r[1] * m.m[1][2] + (s64)r[2] * m.m[2][2]) >> 12);
				lz = (s32)(((s64)f[0] * m.m[0][2] + (s64)f[1] * m.m[1][2] + (s64)f[2] * m.m[2][2]) >> 12);
				h = ratan2(lx, lz) & 0xfff;
				e = abs((((h - a) + 0x800) & 0xfff) - 0x800);
				if (e > worst)
				{
					worst = e;
					printf("ext selftest phys: frame check normal %d %d %d angle %d -> heading %d\n", ns[i].x, ns[i].y, ns[i].z, a, h);
				}
			}
		}
		snprintf(what, sizeof(what), "the kart's forward (RotAxisAngle) has its angle as heading in the surface frame (worst %d of 4096)", worst);
		ExtPhys_TestCheck(worst <= 8, what, &checks, &failures);
	}

	// 4. the zones (B2): reader, surface frame, the zone rule
	{
		ExtPhysParams zp;

		ExtPhys_TestRead("{\"physics\": {\"zones\": [{\"id\": 3, \"air_hold_ms\": 900}, {\"id\": 9, \"gravity_percent\": 150, \"up_blend\": 256, "
		                 "\"max_up_deg\": 120, \"color\": 1}, {\"id\": 0}]}}",
		                 &zp);
		ExtPhys_TestCheck((zp.zoneCount == 2) && zp.anySet && (zp.zones[0].id == 3) && (zp.zones[0].airHoldMs == 900) &&
		                      (zp.zones[0].gravityPercent == 100) && (zp.zones[0].upBlend == EXT_PHYS_ZONE_UP_BLEND) &&
		                      (zp.zones[0].maxUpDeg == EXT_PHYS_ZONE_MAX_UP_DEG) && (zp.zones[1].gravityPercent == 150) && (zp.zones[1].upBlend == 256) &&
		                      (zp.zones[1].maxUpDeg == 120) && (zp.unknownKeys == 1) && (zp.badValues == 1),
		                  "zones: two read with their defaults, an unknown key and a zone id 0 counted", &checks, &failures);
	}
	{
		// the surface frame of floor, slope, wall, overhang (116 deg) and
		// ceiling: unit length, orthogonal, right-handed like the kart's frame,
		// and local -> world -> local back within 2 per 4096
		const SVec3 normals[6] = {{0, 4096, 0}, {0, 2896, -2896}, {4096, 0, 0}, {0, 0, -4096}, {0, -1796, -3681}, {0, -4096, 0}};
		int framesOk = 1;
		int roundOk = 1;

		for (int i = 0; i < 6; i++)
		{
			s32 r[3], f[3];
			const SVec3 n = normals[i];

			ExtPhys_SurfaceFrame(n, r, f);
			{
				const double rr = sqrt((double)r[0] * r[0] + (double)r[1] * r[1] + (double)r[2] * r[2]);
				const double ff = sqrt((double)f[0] * f[0] + (double)f[1] * f[1] + (double)f[2] * f[2]);
				const double rf = ((double)r[0] * f[0] + (double)r[1] * f[1] + (double)r[2] * f[2]) / (4096.0 * 4096.0);
				const double rn = ((double)r[0] * n.x + (double)r[1] * n.y + (double)r[2] * n.z) / (4096.0 * 4096.0);
				const double fn = ((double)f[0] * n.x + (double)f[1] * n.y + (double)f[2] * n.z) / (4096.0 * 4096.0);

				if ((fabs(rr - 4096.0) > 8.0) || (fabs(ff - 4096.0) > 8.0) || (fabs(rf) > 0.003) || (fabs(rn) > 0.003) || (fabs(fn) > 0.003))
				{
					framesOk = 0;
					printf("ext selftest phys: frame of normal %d %d %d: |r| %.1f |f| %.1f r.f %.4f r.n %.4f f.n %.4f\n", n.x, n.y, n.z, rr, ff, rf, rn, fn);
				}
			}
			{
				// the same arithmetic as ExtPhys_ZoneLocalToWorld / WorldToLocal
				const s32 local[3] = {1234, -567, 8901};
				s32 w[3], back[3];

				for (int k = 0; k < 3; k++)
				{
					const s32 nk = (k == 0) ? n.x : ((k == 1) ? n.y : n.z);

					w[k] = (s32)(((s64)r[k] * local[0] + (s64)nk * local[1] + (s64)f[k] * local[2]) >> 12);
				}
				back[0] = (s32)(((s64)r[0] * w[0] + (s64)r[1] * w[1] + (s64)r[2] * w[2]) >> 12);
				back[1] = (s32)(((s64)n.x * w[0] + (s64)n.y * w[1] + (s64)n.z * w[2]) >> 12);
				back[2] = (s32)(((s64)f[0] * w[0] + (s64)f[1] * w[1] + (s64)f[2] * w[2]) >> 12);
				for (int k = 0; k < 3; k++)
				{
					if (abs(back[k] - local[k]) > 8)
					{
						roundOk = 0;
					}
				}
			}
			if (i == 0)
			{
				// on the floor the frame is the world: right +x, forward +z
				framesOk = framesOk && (r[0] == 4096) && (r[1] == 0) && (r[2] == 0) && (f[0] == 0) && (f[1] == 0) && (f[2] == 4096);
			}
		}
		ExtPhys_TestCheck(framesOk, "surface frames of floor, slope, wall, overhang 116 deg, ceiling: orthonormal; the floor's is the world's", &checks,
		                  &failures);
		ExtPhys_TestCheck(roundOk, "local -> world -> local comes back within 8 (of up to 8901) for every frame", &checks, &failures);
	}
	{
		// the zone up: blending towards the ground normal, limited to max_up_deg
		const SVec3 world = {0, 4096, 0};
		const SVec3 wall = {4096, 0, 0};
		const SVec3 ceiling = {0, -4096, 1};
		SVec3 u = world;

		for (int i = 0; i < 40; i++)
		{
			u = ExtPhys_BlendUp(u, wall, 64, 150);
		}
		ExtPhys_TestCheck(ExtPhys_AngleDeg(u, wall) < 1.0, "the zone's up follows a wall normal within 40 frames of blend 64", &checks, &failures);
		// from the wall on towards the ceiling (a linear blend cannot turn an
		// up into its opposite in one go - nor does a surface)
		for (int i = 0; i < 80; i++)
		{
			u = ExtPhys_BlendUp(u, ceiling, 64, 150);
		}
		snprintf(what, sizeof(what), "towards the ceiling the up stops at max_up_deg 150 (%.1f deg)", ExtPhys_AngleDeg(u, world));
		ExtPhys_TestCheck(fabs(ExtPhys_AngleDeg(u, world) - 150.0) < 1.0, what, &checks, &failures);
	}
	{
		// the rule of a frame on a made-up driver: enter on a zone record, hold
		// in the air, leave on a record without a zone or after the air hold
		static struct Driver td;
		const SVec3 slope = {0, 2896, 2896};
		ExtPhysSeatZone *s = &s_extPhysZone[0];

		memset(&s_extPhys, 0, sizeof(s_extPhys));
		memset(s_extPhysZone, 0, sizeof(s_extPhysZone));
		ExtPhys_TestRead("{\"physics\": {\"zones\": [{\"id\": 5, \"air_hold_ms\": 100, \"up_blend\": 256}]}}", &s_extPhys.p);
		memset(&td, 0, sizeof(td));
		ExtPhys_ZoneStep(0, &td, 1, 0, slope, 33);
		ExtPhys_TestCheck(!s->inZone, "ground without a zone: no zone", &checks, &failures);
		ExtPhys_ZoneStep(0, &td, 1, 7, slope, 33);
		ExtPhys_TestCheck(!s->inZone, "a zone id the section does not list: no zone", &checks, &failures);
		ExtPhys_ZoneStep(0, &td, 1, 5, slope, 33);
		snprintf(what, sizeof(what), "on a record of zone 5: in the zone, up = the normal (blend 256) (in %d zone %d up %d %d %d, %.2f deg)", s->inZone, s->zone,
		         s->up.x, s->up.y, s->up.z, ExtPhys_AngleDeg(s->up, slope));
		ExtPhys_TestCheck(s->inZone && (s->zone == 1) && (ExtPhys_AngleDeg(s->up, slope) < 0.5), what, &checks, &failures);
		ExtPhys_ZoneStep(0, &td, 0, 0, slope, 33);
		ExtPhys_ZoneStep(0, &td, 0, 0, slope, 33);
		ExtPhys_ZoneStep(0, &td, 0, 0, slope, 33);
		ExtPhys_TestCheck(s->inZone && (s->airMs == 99), "99 ms in the air: still in the zone", &checks, &failures);
		ExtPhys_ZoneStep(0, &td, 0, 0, slope, 33);
		ExtPhys_TestCheck(!s->inZone && (s->up.y == 4096) && (s->leftZone == 5), "132 ms in the air (hold 100): out, world up", &checks, &failures);
		ExtPhys_ZoneStep(0, &td, 1, 5, slope, 33);
		ExtPhys_ZoneStep(0, &td, 1, 0, slope, 33);
		ExtPhys_TestCheck(!s->inZone, "back in, then on ground without a zone: out at once", &checks, &failures);
		memset(s_extPhysZone, 0, sizeof(s_extPhysZone));
	}
	{
		// the maglev boxes (A2): "on" box -> in the track's first zone with world
		// up, "off" box -> out; the frame's surface has the last word
		static struct Driver tb;
		ExtPhysSeatZone *s = &s_extPhysZone[0];

		memset(&s_extPhys, 0, sizeof(s_extPhys));
		memset(s_extPhysZone, 0, sizeof(s_extPhysZone));
		ExtPhys_TestRead("{\"physics\": {\"zones\": [{\"id\": 5}], \"maglev_volumes\": [{\"name\": \"on\", \"kind\": \"on\", \"center\": [0, 0, 0], \"axes\": "
		                 "[[1, 0, 0], [0, 1, 0], [0, 0, 1]], \"half\": [100, 100, 100]}, {\"name\": \"off\", \"kind\": \"off\", \"center\": [1000, 0, 0], "
		                 "\"axes\": [[1, 0, 0], [0, 1, 0], [0, 0, 1]], \"half\": [100, 100, 100]}]}}",
		                 &s_extPhys.p);
		memset(&tb, 0, sizeof(tb));
		tb.posCurr.x = 500 << 8;
		ExtPhys_MaglevBoxes(0, &tb, 0, 0);
		ExtPhys_TestCheck(!s->inZone, "maglev boxes: between the boxes nothing changes", &checks, &failures);
		tb.posCurr.x = 50 << 8;
		ExtPhys_MaglevBoxes(0, &tb, 1, 0);
		ExtPhys_TestCheck(!s->inZone, "an on box over a road without a zone: no zone", &checks, &failures);
		ExtPhys_MaglevBoxes(0, &tb, 0, 0);
		ExtPhys_TestCheck(s->inZone && (s->zone == 1) && (s->up.y == 4096) && (s->airMs == 0), "an on box in the air: in the first zone, world up", &checks,
		                  &failures);
		tb.posCurr.x = 1000 << 8;
		ExtPhys_MaglevBoxes(0, &tb, 1, 1);
		ExtPhys_TestCheck(s->inZone, "an off box over the zone's own surface: still in", &checks, &failures);
		ExtPhys_MaglevBoxes(0, &tb, 0, 0);
		ExtPhys_TestCheck(!s->inZone && (s->leftZone == 5), "an off box in the air: out", &checks, &failures);
		tb.posCurr.x = 50 << 8;
		s->inZone = 1;
		s->zone = 1;
		ExtPhys_MaglevBoxes(0, &tb, 0, 0);
		ExtPhys_TestCheck(s->inZone, "in a zone an on box changes nothing (hysteresis)", &checks, &failures);
		memset(s_extPhysZone, 0, sizeof(s_extPhysZone));
		memset(&s_extPhys, 0, sizeof(s_extPhys));
	}
	{
		// the maggrav splines (A3): a straight spline along +z (nodes every 100),
		// the local search moves at most one segment a frame, the nearest node
		// over the whole spline, the three directions, the rule of K4
		static struct Driver tg;
		ExtPhysParams *p = &s_extPhys.p;
		ExtPhysMag *m = &s_extPhysMag[0];
		const double far[3] = {0.0, 0.0, 350.0};
		const double side[3] = {50.0, 0.0, 150.0};
		double o[3] = {0.0, 0.0, 0.0};
		s16 dir[3] = {0, 0, 0};
		const SVec3 up = {0, 4096, 0};
		int n1, n2, n3;

		memset(&s_extPhys, 0, sizeof(s_extPhys));
		memset(s_extPhysMag, 0, sizeof(s_extPhysMag));
		ExtPhys_TestRead("{\"physics\": {\"zones\": [{\"id\": 5}], \"maggrav_volumes\": [{\"name\": \"maggrav00\", \"spline\": 0, \"center\": [0, 0, 200], "
		                 "\"axes\": [[1, 0, 0], [0, 1, 0], [0, 0, 1]], \"half\": [500, 500, 500]}], \"maggrav_splines\": [{\"type\": 2, \"nodes\": [[0, 0, 0, "
		                 "0, 1, 0], [0, 0, 100, 0, 1, 0], [0, 0, 200, 1, 0, 0], [0, 0, 300, 1, 0, 0], [0, 0, 400, 1, 0, 0]]}], \"maglev_low_speed\": 6291}}",
		                 p);
		n1 = ExtPhys_SplineLocal(p, 0, 0, far);
		n2 = ExtPhys_SplineLocal(p, 0, n1, far);
		n3 = ExtPhys_SplineLocal(p, 0, n2, far);
		snprintf(what, sizeof(what), "spline: from segment 0 towards z 350 one segment a frame (%d %d %d), the whole spline at once (%d)", n1, n2, n3,
		         ExtPhys_SplineNearest(p, 0, far));
		ExtPhys_TestCheck((n1 == 1) && (n2 == 2) && (n3 == 3) && (ExtPhys_SplineNearest(p, 0, far) == 3) && (ExtPhys_SplineLocal(p, 0, 3, far) == 3), what,
		                  &checks, &failures);
		ExtPhys_TestCheck(ExtPhys_SplineDir(p, 0, 1, side, o) && (fabs(o[0] - 1.0) < 1e-12) && (fabs(o[2]) < 1e-12), "INVPOINT: away from the spline",
		                  &checks, &failures);
		p->splines[0].type = EXT_PHYS_GRAV_POINT;
		ExtPhys_TestCheck(ExtPhys_SplineDir(p, 0, 1, side, o) && (fabs(o[0] + 1.0) < 1e-12), "POINT: towards the spline", &checks, &failures);
		p->splines[0].type = EXT_PHYS_GRAV_PLANE;
		ExtPhys_TestCheck(ExtPhys_SplineDir(p, 0, 1, side, o) && (fabs(o[0] - 0.70710678) < 1e-6) && (fabs(o[1] - 0.70710678) < 1e-6),
		                  "PLANE: the nodes' directions blended at u 0.5, normalised", &checks, &failures);
		ExtPhys_TestCheck(ExtPhys_SplineDir(p, 0, 4, far, o) && (fabs(o[0] - 1.0) < 1e-12), "PLANE at the last node: its own segment (3 -> 4)", &checks,
		                  &failures);
		p->splines[0].type = EXT_PHYS_GRAV_INVPOINT;
		// the box: entering picks the spline and its node, leaving drops them
		memset(&tg, 0, sizeof(tg));
		tg.posCurr.x = 50 << 8;
		tg.posCurr.z = 150 << 8;
		ExtPhys_MaggravStep(0, &tg);
		ExtPhys_TestCheck((m->box == 1) && (m->spline == 1) && (m->node == 1), "maggrav box: spline 0, segment 1 at z 150", &checks, &failures);
		// on the ground under the low speed the zone's up keeps the gravity; in
		// the air, or on the ground at the low speed, the spline's direction
		tg.actionsFlagSet = ACTION_TOUCH_GROUND;
		tg.velocity.z = 6290;
		tg.velocity.y = 9000; // along up: not counted
		ExtPhys_TestCheck(!ExtPhys_SplineGravityWith(p, m, &tg, up, dir), "on the ground at 6290 (< 6291): the zone's up", &checks, &failures);
		tg.velocity.z = 6291;
		ExtPhys_TestCheck(ExtPhys_SplineGravityWith(p, m, &tg, up, dir) && (dir[0] == 4096) && (dir[1] == 0) && (dir[2] == 0),
		                  "on the ground at 6291: the spline's direction (away from it, +x)", &checks, &failures);
		tg.actionsFlagSet = 0;
		tg.velocity.z = 0;
		ExtPhys_TestCheck(ExtPhys_SplineGravityWith(p, m, &tg, up, dir) && (dir[0] == 4096), "in the air at 0: the spline's direction", &checks, &failures);
		p->lowSpeedSet = 0;
		ExtPhys_TestCheck(!ExtPhys_SplineGravityWith(p, m, &tg, up, dir), "without maglev_low_speed: the zone's up (today's rule)", &checks, &failures);
		tg.posCurr.x = 900 << 8;
		ExtPhys_MaggravStep(0, &tg);
		ExtPhys_TestCheck((m->box == 0) && (m->spline == 0) && (m->node == -1), "leaving the box drops spline and node", &checks, &failures);
		memset(s_extPhysMag, 0, sizeof(s_extPhysMag));
		memset(&s_extPhys, 0, sizeof(s_extPhys));
	}
	{
		// gravity volumes: read, a box turned 45 degrees about y, the point test,
		// bad entries counted; without volumes ScaleGravityFor = ScaleGravity
		ExtPhysParams vp;
		static struct Driver tv;
		const double inside[3] = {1000.0 + 140.0, 500.0, 2000.0 + 140.0};   // 198 along the box's first axis
		const double outside[3] = {1000.0 + 140.0, 500.0, 2000.0 - 140.0};  // 198 along the second (half 100)
		const double above[3] = {1000.0, 500.0 + 301.0, 2000.0};

		ExtPhys_TestRead("{\"physics\": {\"gravity_volumes\": [{\"name\": \"low\", \"center\": [1000, 500, 2000], \"axes\": [[1, 0, 1], [-1, 0, 1], "
		                 "[0, 1, 0]], \"half\": [200, 100, 300], \"gravity_percent\": 50}, {\"center\": [0, 0], \"axes\": [], \"half\": [1, 1, 1], "
		                 "\"gravity_percent\": 50}]}}",
		                 &vp);
		ExtPhys_TestCheck((vp.volumeCount == 1) && vp.anySet && (vp.volumes[0].gravityPercent == 50) && (strcmp(vp.volumes[0].name, "low") == 0) &&
		                      (fabs(vp.volumes[0].axis[0][0] - 0.70710678) < 1e-6) && (vp.badValues == 1),
		                  "gravity volumes: one read (axes normalised), a broken one counted", &checks, &failures);
		ExtPhys_TestCheck((ExtPhys_VolumeAtWith(&vp, inside) == 0) && (ExtPhys_VolumeAtWith(&vp, outside) == -1) &&
		                      (ExtPhys_VolumeAtWith(&vp, above) == -1) && (ExtPhys_VolumeAtWith(&vp, vp.volumes[0].center) == 0),
		                  "the turned box holds a point along its long axis, not one along its short axis or above it", &checks, &failures);
		memset(&s_extPhys, 0, sizeof(s_extPhys));
		memset(&tv, 0, sizeof(tv));
		{
			int same = 1;

			for (int g = -0x8000; g <= 0x7fff; g += 13)
			{
				same = same && (ExtPhys_ScaleGravityFor(&tv, g) == g) && (ExtPhys_ScaleGravityFor(NULL, g) == g);
			}
			ExtPhys_TestCheck(same, "without volumes (and without --ext-track) ScaleGravityFor hands every gravity back", &checks, &failures);
		}
	}
	{
		// the source game's anti-gravity data: maglev boxes, maggrav boxes, the
		// splines (an empty entry keeps its index), the low speed; the box test
		// takes its limits inclusive
		static ExtPhysParams ap;
		const double edge[3] = {100.0, 0.0, 0.0};
		const double past[3] = {100.5, 0.0, 0.0};

		ExtPhys_TestRead("{\"physics\": {\"maglev_volumes\": [{\"name\": \"maglev-on01\", \"kind\": \"on\", \"center\": [0, 0, 0], \"axes\": [[2, 0, 0], [0, 1, 0], "
		                 "[0, 0, 1]], \"half\": [100, 50, 50]}, {\"name\": \"maglev-off01\", \"kind\": \"off\", \"center\": [0, 0, 500], \"axes\": [[1, 0, 0], "
		                 "[0, 1, 0], [0, 0, 1]], \"half\": [10, 10, 10]}, {\"kind\": \"maybe\", \"center\": [0, 0, 0], \"axes\": [[1, 0, 0], [0, 1, 0], [0, 0, 1]], "
		                 "\"half\": [1, 1, 1]}], \"maggrav_volumes\": [{\"name\": \"maggrav01\", \"spline\": 1, \"center\": [0, 0, 0], \"axes\": [[1, 0, 0], "
		                 "[0, 1, 0], [0, 0, 1]], \"half\": [10, 10, 10]}], \"maggrav_splines\": [{\"type\": -1, \"nodes\": []}, {\"type\": 2, \"nodes\": "
		                 "[[0, 0, 0, 0, 1, 0], [0, 0, 100, 0, 1, 0], [0, 0, 200, 0, 1, 0]]}, {\"type\": 1, \"nodes\": [[0, 0, 0, 0, 1, 0]]}], "
		                 "\"maglev_low_speed\": 6291}}",
		                 &ap);
		snprintf(what, sizeof(what), "anti-gravity data: 2 maglev boxes (1 on), 1 maggrav box, 3 spline entries (1 usable), low speed (bad %d, unknown %d)",
		         ap.badValues, ap.unknownKeys);
		ExtPhys_TestCheck((ap.maglevCount == 2) && (ap.maglev[0].on == 1) && (ap.maglev[1].on == 0) && (strcmp(ap.maglev[0].box.name, "maglev-on01") == 0) &&
		                      (fabs(ap.maglev[0].box.axis[0][0] - 1.0) < 1e-12) && (ap.maggravCount == 1) && (ap.maggrav[0].spline == 1) &&
		                      (ap.splineCount == 3) && (ap.splines[0].count == 0) && (ap.splines[1].type == 2) && (ap.splines[1].count == 3) &&
		                      (ap.splines[1].first == 0) && (ap.splines[2].count == 0) && (ap.nodeCount == 3) && (ap.nodes[2][2] == 200.0) && ap.lowSpeedSet &&
		                      (ap.lowSpeed == 6291) && (ap.badValues == 2) && (ap.unknownKeys == 0) && !ap.anySet,
		                  what, &checks, &failures);
		ExtPhys_TestCheck(ExtPhys_InBox(&ap.maglev[0].box, edge) && !ExtPhys_InBox(&ap.maglev[0].box, past), "the box test holds its edge, not 0.5 past it",
		                  &checks, &failures);
	}
	{
		// outside the zones every zone hook is inert: no zones read, nothing in force
		static struct Driver td2;
		SVec3 up = {1, 2, 3};
		s16 g[3] = {0, 0, 0};
		const s32 local[3] = {1, 2, 3};
		Vec3 world = {7, 8, 9};

		memset(&s_extPhys, 0, sizeof(s_extPhys));
		memset(&td2, 0, sizeof(td2));
		ExtPhys_TestCheck(!ExtPhys_DriverUp(&td2, &up) && (up.x == 0) && (up.y == 4096) && (up.z == 0) && (ExtPhys_DriverZone(&td2) == 0) &&
		                      !ExtPhys_ZoneGravity(&td2, -900, g) && (g[1] == 0) && !ExtPhys_ZoneLocalToWorld(&td2, local, &world) && (world.x == 7) &&
		                      (world.y == 8) && (world.z == 9),
		                  "without zones: DriverUp gives world up, no zone gravity, no frame turn", &checks, &failures);
		{
			// the ground probe: nothing without a driver, a position or the
			// external collision; the probe's up is the hint outside a zone
			const s32 at[3] = {0, 0, 0};
			const SVec3 hint = {0, 0, 4096};
			SVec3 probeUp = {0, 0, 0};
			ExtCollHit h;

			ExtPhys_TestCheck(!ExtPhys_GroundProbe(NULL, at, NULL, &h, NULL) && !ExtPhys_GroundProbe(&td2, NULL, NULL, &h, NULL) &&
			                      !ExtPhys_GroundProbe(&td2, at, &hint, &h, &probeUp) && (probeUp.z == 4096) && (probeUp.y == 0),
			                  "ground probe: none without driver, position or built collision; outside a zone along the hint", &checks, &failures);
		}
	}

	memset(&s_extPhys, 0, sizeof(s_extPhys));
	if (failures)
	{
		printf("ext selftest phys FAILED: %d checks, %d failures\n", checks, failures);
		return 1;
	}
	printf("ext selftest phys passed: %d checks, 0 failures\n", checks);
	return 0;
}
