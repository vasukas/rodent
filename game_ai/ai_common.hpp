#ifndef AI_COMMON_HPP
#define AI_COMMON_HPP

#include <variant>
#include "game/entity.hpp"
#include "game/level_ctr.hpp"

class AI_Drone;
class AI_Group;



enum class AI_Speed
{
	Slow,
	Normal,
	Accel,
	
	TOTAL_COUNT
};



struct AI_DroneParams
{
	std::array<float, static_cast<size_t>(AI_Speed::TOTAL_COUNT)> speed = {1, 1, 1};
	
	float dist_minimal = 5;  ///< If closer, moves away
	float dist_optimal = 10; ///< If further, moves closer
	float dist_visible = 20; ///< If further, doesn't post or attack target
	float dist_suspect = 24; ///< Max visibility distance
	
	bool is_camper = false; ///< If true, pursues only visible targets
};



/// Ai-specific constants
namespace AI_Const
{

/// Target lock length
const TimeSpan target_switch_timeout = TimeSpan::seconds(1.5);

/// If distance between locked and nearest targets is bigger, re-locks to nearest
const float target_switch_distance = 3;



/// Time to fully update target velocity accumulator
const TimeSpan attack_acc_adjust_time = TimeSpan::seconds(0.5);

// Attack correction TTR deviations - min and max
const float attack_ttr_dev0 = 0.1;
const float attack_ttr_dev1 = 0.25;

/// Fire LoS raycast width
const float attack_los_hwidth = 0.3;



/// Minimal distance for evade raycast
const float mov_steer_dist = 4;

/// Multiplier for evade velocity calculation
const float mov_steer_force = 10;

/// Minimal multiplier for evade velocity
const float mov_steer_min_t = 0.5;



/// Time to wait before moving due to crowd
const TimeSpan pos_req_crowd_time = TimeSpan::seconds(0.3);

/// Time to wait before moving due to obstructed fire LoS
const TimeSpan pos_req_los_time = TimeSpan::seconds(0.5);

/// Time to remember previous moving direction
const TimeSpan pos_req_mem_time = TimeSpan::seconds(1);

/// Delay before resetting moving
const TimeSpan pos_req_reset_delay = TimeSpan::seconds(0.3);

/// Drones should have more distance between each other
const float crowd_distance = 3.5;



/// Time before chasing
const TimeSpan chase_camp_time = TimeSpan::seconds(1.5);

/// How fast chasing time decreases then target is visible
const float chase_camp_decr = 0.05;

/// Additional distance to target, to move bit more forward
const float chase_ahead_dist = 7;



/// Time to wait on point when searching
const TimeSpan search_point_wait = TimeSpan::seconds(2);

/// How long to wait on suspect's position
const TimeSpan suspect_wait = TimeSpan::seconds(3);



/// Time to wait before another inspect can begin
const TimeSpan proxy_inspect_timeout = TimeSpan::seconds(2);

/// Time to wait before going idle after all drones arrived at final points
const TimeSpan group_search_time = TimeSpan::seconds(5);



/// If target no visible for that time, it's considered lost
const TimeSpan grouptarget_lost_time = TimeSpan::seconds(0.5);

/// How often AoS can be updated
const TimeSpan aos_update_timeout = TimeSpan::seconds(0.3);

/// After that time without LoS to target drone considered left AoS
const TimeSpan aos_leave_timeout = TimeSpan::seconds(0.2);

/// Default search rings
const std::array<int, 2> search_ring_dist = {7, 20};
}

#endif // AI_COMMON_HPP
