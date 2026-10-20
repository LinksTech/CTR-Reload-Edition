#include <common.h>
#include <platform/native_extphys.h>

enum
{
	VEH_AFTER_COLL_SCRUB_COUNT = len(data.MetaDataScrub),
	VEH_AFTER_COLL_TERRAIN_COUNT = len(data.MetaDataTerrain),
};

CTR_STATIC_ASSERT(VEH_AFTER_COLL_TERRAIN_COUNT == TERRAIN_SLOWDIRT + 1);

struct Scrub *VehAfterColl_GetSurface(u32 scrubId)
{
	struct Scrub *sc = &data.MetaDataScrub[0];

	if (scrubId < VEH_AFTER_COLL_SCRUB_COUNT)
	{
		return &sc[scrubId];
	}

	return sc;
}

struct Terrain *VehAfterColl_GetTerrain(u8 terrainType)
{
	struct Terrain *ter = &data.MetaDataTerrain[0];

	// if terrain is valid, max 20
	if (terrainType < VEH_AFTER_COLL_TERRAIN_COUNT)
	{
		ter = &ter[terrainType];
	}

	// The external track's own terrain values (physics.terrain): a copy of the
	// row; without --ext-track or without own values the row itself.
	return ExtPhys_Terrain(terrainType, ter);
}
