// rldhires_hull.inc - the animated hull of a hi-res driver: the 4 m rule over
// the rest pose and every clip, for rldpack and Reload Studio
//
// AN INCLUDE, NOT A MODULE, header-only, after include/rldhires.inc and
// include/rldhires_anim.inc (with cgltf.h before both: the build half of the
// pose maths). Kept out of rldhires.inc so that the shared rules do not need
// the pose maths.
//
// THE RULE (HRG-18, the part a bind pose cannot show): the model with every
// clip stays inside 4 m on each axis. The box is the engine's hull
// (platform/engine_hires_char.c, EngHc_Hull) without its culling margin: per
// skinning entry - a joint of the skin, a body node with a mesh but no skin -
// the box of the vertices it moves (a skinned vertex: every joint with a weight
// above 0; weights that sum to 0 count as joint 0, the engine's rule), carried
// through the rest pose and every clip at RLDHIRES_HULL_SAMPLES evenly spaced
// times (both ends included) by that entry's matrix; the wheels as their middle
// plus and minus their radius (own wheels: the node's posed middle and its rest
// radius; else the engine's standard wheels at the retail points, 16 model
// units). The engine builds its skeleton and clips with the same functions, so
// a clip it could not read is refused here too (HRG-13 the skeleton, HRG-7 a
// clip).

#ifndef RLDHIRES_HULL_INC
#define RLDHIRES_HULL_INC

// The middle of a standard wheel in model units at scale 0x1000: the integer
// rule of Engine_HiresWheelMiddle (platform/engine_hires.c) - (+-0x90, 0x40,
// 0xc7 front | -0x60 rear) / 4, +X for wheels 0 and 2.
static void RldHiresHull_StdMiddle(int wheel, float out[3])
{
	const int x = 0x90;
	const int y = 0x40;
	const int z = (wheel < 2) ? 0xc7 : -0x60;

	out[0] = (float)(((wheel & 1) == 0) ? x : -x) * 0.25f;
	out[1] = (float)y * 0.25f;
	out[2] = (float)z * 0.25f;
}

static void RldHiresHull_Grow(float lo[3], float hi[3], const float p[3])
{
	int a;

	for (a = 0; a < 3; a++)
	{
		lo[a] = (p[a] < lo[a]) ? p[a] : lo[a];
		hi[a] = (p[a] > hi[a]) ? p[a] : hi[a];
	}
}

// The vertex boxes of one primitive into the entries' boxes (local space).
static void RldHiresHull_Boxes(const cgltf_primitive *prim, int skinned, u32 rigidEntry, u32 entries, float *box)
{
	const cgltf_accessor *pos = cgltf_find_accessor(prim, cgltf_attribute_type_position, 0);
	const cgltf_accessor *jo = skinned ? cgltf_find_accessor(prim, cgltf_attribute_type_joints, 0) : NULL;
	const cgltf_accessor *we = skinned ? cgltf_find_accessor(prim, cgltf_attribute_type_weights, 0) : NULL;
	cgltf_size v;

	if ((pos == NULL) || (prim->type != cgltf_primitive_type_triangles))
	{
		return;
	}
	for (v = 0; v < pos->count; v++)
	{
		float p[3] = {0.0f, 0.0f, 0.0f};
		int k;

		cgltf_accessor_read_float(pos, v, p, 3);
		if ((jo != NULL) && (we != NULL) && (jo->count == pos->count) && (we->count == pos->count))
		{
			cgltf_uint j[4] = {0, 0, 0, 0};
			float w[4] = {0.0f, 0.0f, 0.0f, 0.0f};

			cgltf_accessor_read_uint(jo, v, j, 4);
			cgltf_accessor_read_float(we, v, w, 4);
			if (!((w[0] + w[1] + w[2] + w[3]) > 0.0f))
			{
				w[0] = 1.0f; // the engine's rule for weights that sum to 0: all on the first joint
			}
			for (k = 0; k < 4; k++)
			{
				const u32 e = (j[k] < 255u) ? (u32)j[k] : 0u;

				if ((w[k] > 0.0f) && (e < entries))
				{
					RldHiresHull_Grow(&box[e * 6u], &box[e * 6u + 3u], p);
				}
			}
		}
		else if (rigidEntry < entries)
		{
			RldHiresHull_Grow(&box[rigidEntry * 6u], &box[rigidEntry * 6u + 3u], p);
		}
	}
}

// The animated hull of an opened model (RldHires_Open said usable) in metres,
// into lo/hi; HRG-18 (and HRG-13 / HRG-7 for what cannot be posed) into rep.
// 1 = within 4 m on every axis; 0 = refused. *worstClip (may be NULL): the clip
// whose poses first passed 4 m, -1 = the rest pose or none.
static int RldHires_CheckClipHull(const RldHiresModel *m, RldHiresReport *rep, float lo[3], float hi[3], int *worstClip)
{
	const cgltf_data *gd = m->data;
	RldAnimSkeleton skel;
	RldAnimClip clip[RLDHIRES_CLIP_COUNT];
	RldAnimTrs *pose = NULL;
	float *worlds = NULL;
	float *box = NULL;
	u32 *rigidNode = NULL;
	u32 joints;
	u32 rigid = 0;
	u32 entries;
	float wheelRadius[RLDHIRES_WHEEL_COUNT];
	const int ownWheels = (m->wheelNode[0] >= 0);
	const char *why = "";
	int ok = 1;
	int over = 0;
	int first = -1;
	int sample;
	int k;
	u32 e;
	cgltf_size n;

	memset(&skel, 0, sizeof(skel));
	memset(clip, 0, sizeof(clip));
	lo[0] = lo[1] = lo[2] = 1e30f;
	hi[0] = hi[1] = hi[2] = -1e30f;
	if (worstClip != NULL)
	{
		*worstClip = -1;
	}
	if ((gd == NULL) || (m->drawNodes == NULL))
	{
		return 0;
	}
	if (!RldAnim_SkeletonFromGltf(gd, (m->skin >= 0) ? &gd->skins[m->skin] : NULL, &skel, &why))
	{
		RldHires_Say(rep, "HRG-13", "the skeleton cannot be posed (%s) - export the armature again", why);
		return 0;
	}
	for (k = 0; k < RLDHIRES_CLIP_COUNT; k++)
	{
		u32 skipped = 0;

		if ((m->clipAnim[k] >= 0) && !RldAnim_ClipFromGltf(gd, &gd->animations[m->clipAnim[k]], &clip[k], &skipped, &why))
		{
			RldHires_Say(rep, "HRG-7", "the clip '%s' cannot be read (%s) - export it again", s_rldHiresClipName[k], why);
			ok = 0;
		}
	}

	// The entries: the joints, then the body nodes with a mesh and no skin.
	joints = (m->skin >= 0) ? m->joints : 0u;
	rigidNode = (u32 *)malloc((size_t)(m->drawNodeCount + 1) * sizeof(u32));
	box = (float *)malloc(((size_t)joints + (size_t)m->drawNodeCount + 1u) * 6u * sizeof(float));
	pose = (RldAnimTrs *)malloc((size_t)skel.nodeCount * sizeof(RldAnimTrs));
	worlds = (float *)malloc((size_t)skel.nodeCount * 16u * sizeof(float));
	if (!ok || (rigidNode == NULL) || (box == NULL) || (pose == NULL) || (worlds == NULL))
	{
		if (ok)
		{
			RldHires_Say(rep, "HRG-7", "out of memory posing the clips");
		}
		ok = 0;
		goto done;
	}
	for (n = 0; n < (cgltf_size)m->drawNodeCount; n++)
	{
		if (gd->nodes[m->drawNodes[n]].skin == NULL)
		{
			rigidNode[rigid++] = (u32)m->drawNodes[n];
		}
	}
	entries = joints + rigid;
	for (e = 0; e < entries; e++)
	{
		box[e * 6u + 0u] = box[e * 6u + 1u] = box[e * 6u + 2u] = 1e30f;
		box[e * 6u + 3u] = box[e * 6u + 4u] = box[e * 6u + 5u] = -1e30f;
	}
	for (n = 0; n < (cgltf_size)m->drawNodeCount; n++)
	{
		const cgltf_node *node = &gd->nodes[m->drawNodes[n]];
		const int skinned = (node->skin != NULL);
		u32 entry = 0;
		cgltf_size p;
		u32 r;

		for (r = 0; !skinned && (r < rigid); r++)
		{
			if (rigidNode[r] == (u32)m->drawNodes[n])
			{
				entry = joints + r;
			}
		}
		for (p = 0; p < node->mesh->primitives_count; p++)
		{
			RldHiresHull_Boxes(&node->mesh->primitives[p], skinned, entry, entries, box);
		}
	}

	// The wheel radius at rest, model units (the engine's: mesh radius x the node's Y scale x 64).
	RldAnim_PoseRest(&skel, pose);
	RldAnim_NodeMatrices(&skel, pose, worlds);
	for (k = 0; k < RLDHIRES_WHEEL_COUNT; k++)
	{
		wheelRadius[k] = 16.0f;
		if (ownWheels)
		{
			const float *w = &worlds[(size_t)m->wheelNode[k] * 16u];

			wheelRadius[k] = (float)((double)m->wheelRadius[k] * sqrt((double)w[4] * w[4] + (double)w[5] * w[5] + (double)w[6] * w[6]) * RLDHIRES_UNITS_PER_METRE);
		}
	}

	// The rest pose (-1), then every clip at its samples.
	for (sample = -1; sample < RLDHIRES_CLIP_COUNT * RLDHIRES_HULL_SAMPLES; sample++)
	{
		const int c = (sample < 0) ? -1 : (sample / RLDHIRES_HULL_SAMPLES);

		if ((c >= 0) && (m->clipAnim[c] < 0))
		{
			continue;
		}
		RldAnim_PoseRest(&skel, pose);
		if (c >= 0)
		{
			const RldAnimClip *cl = &clip[c];
			const float t = cl->start + (cl->end - cl->start) * (float)(sample % RLDHIRES_HULL_SAMPLES) / (float)(RLDHIRES_HULL_SAMPLES - 1);

			RldAnim_SampleClip(cl, t, pose, skel.nodeCount);
		}
		RldAnim_NodeMatrices(&skel, pose, worlds);
		for (e = 0; e < entries; e++)
		{
			const float *b = &box[e * 6u];
			float mtx[16];
			int corner;

			if (b[0] > b[3])
			{
				continue;
			}
			if (e < joints)
			{
				RldAnim_JointMatrix(&skel, worlds, e, mtx);
			}
			else
			{
				memcpy(mtx, &worlds[(size_t)rigidNode[e - joints] * 16u], sizeof(mtx));
			}
			for (corner = 0; corner < 8; corner++)
			{
				const float x = (corner & 1) ? b[3] : b[0];
				const float y = (corner & 2) ? b[4] : b[1];
				const float z = (corner & 4) ? b[5] : b[2];
				float q[3];
				int a;

				for (a = 0; a < 3; a++)
				{
					q[a] = (mtx[a] * x + mtx[4 + a] * y + mtx[8 + a] * z + mtx[12 + a]) * RLDHIRES_UNITS_PER_METRE;
				}
				RldHiresHull_Grow(lo, hi, q);
			}
		}
		for (k = 0; k < RLDHIRES_WHEEL_COUNT; k++)
		{
			float mid[3];
			int a;

			if (ownWheels)
			{
				const float *w = &worlds[(size_t)m->wheelNode[k] * 16u];

				mid[0] = w[12] * RLDHIRES_UNITS_PER_METRE;
				mid[1] = w[13] * RLDHIRES_UNITS_PER_METRE;
				mid[2] = w[14] * RLDHIRES_UNITS_PER_METRE;
			}
			else
			{
				RldHiresHull_StdMiddle(k, mid);
			}
			for (a = 0; a < 3; a++)
			{
				float pl[3];
				float mi[3];

				memcpy(pl, mid, sizeof(pl));
				memcpy(mi, mid, sizeof(mi));
				pl[a] += wheelRadius[k];
				mi[a] -= wheelRadius[k];
				RldHiresHull_Grow(lo, hi, pl);
				RldHiresHull_Grow(lo, hi, mi);
			}
		}
		if (!over)
		{
			int a;

			for (a = 0; a < 3; a++)
			{
				if (!((hi[a] - lo[a]) <= RLDHIRES_BOUNDS_MAX_M * RLDHIRES_UNITS_PER_METRE))
				{
					over = 1;
					first = c;
					break;
				}
			}
		}
	}
	for (k = 0; k < 3; k++)
	{
		lo[k] /= RLDHIRES_UNITS_PER_METRE;
		hi[k] /= RLDHIRES_UNITS_PER_METRE;
	}
	if (over)
	{
		int a;
		int worst = 0;

		for (a = 1; a < 3; a++)
		{
			worst = ((hi[a] - lo[a]) > (hi[worst] - lo[worst])) ? a : worst;
		}
		RldHires_Say(rep, "HRG-18", "with its clips the model reaches %.2f m along %c (first passed in '%s') - at most 4 m; keep every pose inside 4 m",
		             (double)(hi[worst] - lo[worst]), "XYZ"[worst], (first >= 0) ? s_rldHiresClipName[first] : "the rest pose with its wheels");
		if (worstClip != NULL)
		{
			*worstClip = first;
		}
		ok = 0;
	}

done:
	free(pose);
	free(worlds);
	free(box);
	free(rigidNode);
	for (k = 0; k < RLDHIRES_CLIP_COUNT; k++)
	{
		RldAnim_FreeClip(&clip[k]);
	}
	RldAnim_FreeSkeleton(&skel);
	return ok;
}

#endif // RLDHIRES_HULL_INC
