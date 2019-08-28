#ifndef COMMON_DEFS_HPP
#define COMMON_DEFS_HPP

#include "vaslib/vas_time.hpp"
#include "vaslib/vas_types.hpp"

namespace GameConst
{
// physical radiuses
const float hsz_supply = 0.5; ///< Radius of supplies and powerups
const float hsz_proj = 0.3; ///< Radius of normal projectile
const float hsz_proj_big = 0.45; ///< Radius of big projectile

const float hsz_rat = 0.7; ///< Radius of PC
const float hsz_box_small = 0.7; ///< Radius of small box
}

enum : size_t
{
	TEAM_ENVIRON = 0, ///< Environment
	TEAM_BOTS = 1,
	TEAM_PLAYER = 2,
	TEAM_FREEWPN = 3 ///< Weapon without source
};

#endif // COMMON_DEFS_HPP
