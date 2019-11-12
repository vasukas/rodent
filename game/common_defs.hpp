#ifndef COMMON_DEFS_HPP
#define COMMON_DEFS_HPP

#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"
#include "vaslib/vas_types.hpp"

namespace GameConst
{
// physical radiuses

const float hsz_supply = 0.5; ///< Supplies
const float hsz_supply_big = 1; ///< Powerups etc
const float hsz_proj = 0.3; ///< Normal projectile
const float hsz_proj_big = 0.45; ///< Big projectile

const float hsz_rat = 0.7; ///< PC
const float hsz_box_small = 0.85;
const float hsz_drone = 0.6;
const float hsz_drone_big = 0.8;

const float hsz_cell_tmp = 1.5; ///< temporary
const float hsz_interact = 1.3;
const float hsz_termfin = 2;

const vec2fp hsz_pshl = {0.7, 1.2}; ///< Projected shield

// other consts

const size_t total_key_count = 8;
}



enum : size_t
{
	TEAM_ENVIRON = 0, ///< Environment
	TEAM_BOTS = 1,
	TEAM_PLAYER = 2
};

#endif // COMMON_DEFS_HPP
