#ifndef PRESENTER_RES_HPP
#define PRESENTER_RES_HPP

#include "vaslib/vas_math.hpp"

enum ObjEffect
{
	OE_DEATH,
	OE_DUST
};
enum FreeEffect
{
	FE_EXPLOSION,
	FE_HIT,
	FE_SHOOT_DUST
};
enum ObjList
{
	OBJ_PC,
	OBJ_BOX,
	OBJ_HEAVY,
	PROJ_ROCKET,
//	PROJ_RAY,
	PROJ_BULLET,
	PROJ_PLASMA,
	ARM_SHIELD,
	ARM_ROCKET,
	ARM_MGUN,
//	ARM_RAYGUN,
	ARM_PLASMA,
	
	OBJ_ALL_WALLS
};

struct GameResBase
{
	const float hsz_box = 0.7;
	const float hsz_heavy = 1.5;
	const float hsz_rat = 0.6;
	const float hsz_proj = 0.2;
	const vec2fp hsz_shld = {2, 0.7};
	
	static GameResBase& get();
	void init_res();
};

#endif // PRESENTER_RES_HPP
