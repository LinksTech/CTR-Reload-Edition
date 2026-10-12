#include <platform/engine.h>
#include <platform/native_probe.h>
#include <platform/native_tex.h>

// ENGINE TESTSCENE - see include/platform/engine.h.
//
// A textured box (the native probe body, unit box x/z -1..1, y 0..1) 600 units
// in front of the camera of view 0, turning slowly with the game timer. It goes
// through the whole engine path: material and mesh tables, the upload at the
// load stage, a draw submitted into the scene and linked into the ordering
// table of the view's push buffer, drawn by the OT parser.

#define ENGINE_TESTSCENE_DISTANCE 600.0f // view z of the box's centre
#define ENGINE_TESTSCENE_SCALE 200.0f
#define ENGINE_TESTSCENE_TURN_PER_TICK 0.02f // radians of yaw per game tick

internal int s_testMesh = ENGINE_MESH_INVALID;
internal int s_testMaterial = -1;
internal unsigned long long s_testFramesDrawn = 0;
internal int s_testLogged = 0;

CTR_STATIC_ASSERT(sizeof(struct NativeProbeVertex) == sizeof(EngineVertex));

// General affine inverse of a view matrix (3x3 A and translation t): inv(A) and
// -inv(A) t. A transpose is NOT enough: the game's matrix_ViewProj has its Y row
// scaled for the aspect (about 0x360/0x600), so it is not orthonormal; with the
// transpose the translation's y came back scaled by s^2 instead of cancelled and
// the box landed thousands of units off screen. Returns 0 (identity in out) when
// the matrix is singular.
internal int Engine_TestSceneInvertRigid(const EngineMat4 *v, EngineMat4 *out)
{
	// a(r, c) = v->m[c * 4 + r] (column-major)
	const double a00 = v->m[0], a10 = v->m[1], a20 = v->m[2];
	const double a01 = v->m[4], a11 = v->m[5], a21 = v->m[6];
	const double a02 = v->m[8], a12 = v->m[9], a22 = v->m[10];
	const double c00 = a11 * a22 - a12 * a21;
	const double c01 = a12 * a20 - a10 * a22;
	const double c02 = a10 * a21 - a11 * a20;
	const double det = a00 * c00 + a01 * c01 + a02 * c02;
	double inv[3][3];
	int i;

	Engine_Mat4Identity(out);
	if ((det < 1e-12) && (det > -1e-12))
	{
		return 0;
	}
	inv[0][0] = c00 / det;
	inv[0][1] = (a02 * a21 - a01 * a22) / det;
	inv[0][2] = (a01 * a12 - a02 * a11) / det;
	inv[1][0] = c01 / det;
	inv[1][1] = (a00 * a22 - a02 * a20) / det;
	inv[1][2] = (a02 * a10 - a00 * a12) / det;
	inv[2][0] = c02 / det;
	inv[2][1] = (a01 * a20 - a00 * a21) / det;
	inv[2][2] = (a00 * a11 - a01 * a10) / det;
	for (i = 0; i < 3; i++)
	{
		int k;

		for (k = 0; k < 3; k++)
		{
			out->m[k * 4 + i] = (float)inv[i][k];
		}
		out->m[12 + i] = (float)-(inv[i][0] * v->m[12] + inv[i][1] * v->m[13] + inv[i][2] * v->m[14]);
	}
	return 1;
}

void Engine_TestSceneLoad(void)
{
	NativeTexDesc texDesc;
	NativeTexResult texResult = NATIVE_TEX_OK;
	TextureID texture;
	EngineMaterial material;
	EngineRange range;
	EngineMeshDesc mesh;

	// Once per load: a second load has had its tables reset by
	// Engine_RenderLevelUnload; a table that still holds the mesh keeps it.
	if ((s_testMesh != ENGINE_MESH_INVALID) && (Engine_MeshDesc(s_testMesh) != NULL))
	{
		return;
	}

	memset(&texDesc, 0, sizeof(texDesc));
	texDesc.width = NATIVE_PROBE_TEXTURE_SIZE;
	texDesc.height = NATIVE_PROBE_TEXTURE_SIZE;
	texDesc.rgba = NativeProbe_TexturePixels();
	texDesc.flags = 0; // sRGB colours, levels computed
	texDesc.wrapU = NATIVE_GFX_WRAP_CLAMP;
	texDesc.wrapV = NATIVE_GFX_WRAP_CLAMP;
	texDesc.name = "engine test scene";
	texture = NativeTex_Create(&texDesc, &texResult);
	if (texture == NATIVE_GFX_INVALID)
	{
		Platform_Log("[CTR Engine] test scene: texture refused (%s)\n", NativeTex_ResultName(texResult));
		return;
	}

	memset(&material, 0, sizeof(material));
	material.blend = ENGINE_BLEND_OPAQUE;
	material.texSource = ENGINE_TEX_NATIVE;
	// Double sided: the model matrix mirrors the probe body (view y points
	// down), which flips the winding of every face.
	material.flags = ENGINE_MATERIAL_SRGB | ENGINE_MATERIAL_DOUBLE_SIDED;
	material.nativeTexture = (u32)texture;
	material.tint[0] = material.tint[1] = material.tint[2] = material.tint[3] = 1.0f;
	s_testMaterial = Engine_MaterialRegister(&material);

	memset(&range, 0, sizeof(range));
	range.firstIndex = 0;
	range.indexCount = NATIVE_PROBE_INDEX_COUNT;
	range.material = (u16)s_testMaterial;

	memset(&mesh, 0, sizeof(mesh));
	mesh.vertices = (const EngineVertex *)NativeProbe_TexturedVertices();
	mesh.verticesPerFrame = NATIVE_PROBE_VERTEX_COUNT;
	mesh.frames = 1;
	mesh.indices = NativeProbe_Indices();
	mesh.indexCount = NATIVE_PROBE_INDEX_COUNT;
	mesh.ranges = &range;
	mesh.rangeCount = 1;
	mesh.name = "engine test box";
	s_testMesh = Engine_MeshCreate(&mesh);

	Platform_Log("[CTR Engine] test scene: mesh %d, material %d\n", s_testMesh, s_testMaterial);
}

void Engine_TestSceneCollect(struct GameTracker *gGT)
{
	const EngineCamera *cam;
	EngineMat4 screenFromWorld;
	EngineMat4 worldFromView;
	EngineMat4 model;
	EngineMat4 viewModel;
	EngineMat4 clip;
	EngineDraw draw;
	float yaw;
	float c;
	float s;
	int cell;

	// Only in a race: not the menus, a level loaded, a mesh made.
	if (((gGT->gameMode1 & MAIN_MENU) != 0) || (gGT->level1 == NULL) || (s_testMesh == ENGINE_MESH_INVALID))
	{
		return;
	}
	cam = Engine_FrameCamera(0);
	if (cam == NULL)
	{
		return;
	}

	// Model in view space: yaw about the vertical axis, scaled (y negative:
	// view y points down, the box stands up on screen), the box's middle moved
	// to the origin, then 600 units in front of the camera.
	yaw = (float)gGT->timer * ENGINE_TESTSCENE_TURN_PER_TICK;
	c = cosf(yaw) * ENGINE_TESTSCENE_SCALE;
	s = sinf(yaw) * ENGINE_TESTSCENE_SCALE;
	Engine_Mat4Identity(&model);
	// columns: x axis, y axis, z axis, translation
	model.m[0] = c;
	model.m[2] = -s;
	model.m[5] = -ENGINE_TESTSCENE_SCALE;
	model.m[8] = s;
	model.m[10] = c;
	// box middle (0, 0.5, 0) to the origin, then to (0, 0, distance)
	model.m[13] = 0.5f * ENGINE_TESTSCENE_SCALE;
	model.m[14] = ENGINE_TESTSCENE_DISTANCE;
	viewModel = model;

	// clipFromModel = screenFromWorld * worldFromView * viewModel
	Engine_CameraScreenFromWorld(cam, &screenFromWorld);
	if (!Engine_TestSceneInvertRigid(&cam->viewFromWorld, &worldFromView))
	{
		return;
	}
	Engine_Mat4Mul(&worldFromView, &viewModel, &model);
	Engine_Mat4Mul(&screenFromWorld, &model, &clip);

	memset(&draw, 0, sizeof(draw));
	draw.mesh = s_testMesh;
	draw.frame = 0;
	draw.clipFromModel = clip;
	draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
	draw.materialOverride = -1;
	draw.sortDepth = ENGINE_TESTSCENE_DISTANCE;
	// OT cell: the level's rule, view z >> 6 (DrawLevelOvr1P.c: otIndex =
	// (maxDepth >> 6) + drawOrder); a higher cell is farther.
	cell = (int)(ENGINE_TESTSCENE_DISTANCE) >> 6;
	if (cell < 0)
	{
		cell = 0;
	}
	if (cell > 0x3fe)
	{
		cell = 0x3fe;
	}
	draw.otCell = (u32)cell;
	draw.dynMaterial = -1;

	if (Engine_RenderSubmitLinked(0, &draw, gGT->pushBuffer[0].ptrOT, cell))
	{
		s_testFramesDrawn++;
		if (!s_testLogged)
		{
			s_testLogged = 1;
			Platform_Log("[CTR Engine] test scene: first draw submitted, view 0, OT cell %d\n", cell);
		}
	}
}
