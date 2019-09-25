#ifndef COMMON_DEFS_HPP
#define COMMON_DEFS_HPP

#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"
#include "vaslib/vas_types.hpp"

namespace GameConst
{
// physical radiuses
const float hsz_supply = 0.5; ///< Supplies and powerups
const float hsz_proj = 0.3; ///< Normal projectile
const float hsz_proj_big = 0.45; ///< Big projectile

const float hsz_rat = 0.7; ///< PC
const float hsz_box_small = 0.85;
const float hsz_drone = 0.6;

const vec2fp hsz_pshl = {0.7, 1.2}; ///< Projected shield
}

enum : size_t
{
	TEAM_ENVIRON = 0, ///< Environment
	TEAM_BOTS = 1,
	TEAM_PLAYER = 2,
	TEAM_FREEWPN = 3 ///< Weapon without source
};

#endif // COMMON_DEFS_HPP
