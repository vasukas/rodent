#ifndef AI_COMMON_HPP
#define AI_COMMON_HPP

#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"

class AI_Drone;



enum class AI_Speed
{
	SlowPrecise, ///< Zero inertia
	Slow,
	Normal,
	Patrol, ///< Imprecise target point
	Accel,
	
	TOTAL_COUNT
};



struct AI_DroneParams
{
	enum HelpCallPrio : uint8_t {
		HELP_NEVER,  // never selected for help
		HELP_LOW,    // selected only if nothing better is in range
		HELP_ALWAYS  // nearest always selected
	};
	
	std::array<float, static_cast<size_t>(AI_Speed::TOTAL_COUNT)> speed = {};
	
	float dist_panic   = 0;  ///< If closer, moves away fast and doesn't shoot
	float dist_minimal = 5;  ///< If closer, moves away
	float dist_optimal = 10; ///< If further, moves closer
	float dist_visible = 20; ///< If further, doesn't post or attack target
	float dist_suspect = 24; ///< Max visibility distance
	std::optional<float> dist_battle = {}; ///< Visibility distance while in battle mode
	
	bool is_camper = false; ///< If true, pursues only visible targets
	HelpCallPrio helpcall = HELP_ALWAYS;
	uint8_t placement_prio    = 1; ///< Must be non-zero. Higher gets better placement on attack
	uint8_t placement_freerad = 0; ///< Cells in that radius (square, not circle) are marked as used by this drone (but not rays)
	
	float rot_speed = M_PI*2; ///< Max rotation delta per second, radians. May be zero
	
	/// Minimal and maximal field-of-view half-angles, radians
	std::optional<std::pair<float, float>> fov = std::make_pair(deg_to_rad(30), deg_to_rad(60));
	
	
	///
	void set_speed(float slow, float normal, float high)
	{
		speed[static_cast<size_t>(AI_Speed::Slow)] = slow;
		speed[static_cast<size_t>(AI_Speed::SlowPrecise)] = slow;
		speed[static_cast<size_t>(AI_Speed::Normal)] = normal;
		speed[static_cast<size_t>(AI_Speed::Patrol)] = lerp(slow, normal, 0.5);
		speed[static_cast<size_t>(AI_Speed::Accel)] = high;
	}
	float get_speed(AI_Speed i) const {return speed.at(static_cast<size_t>(i));}
};



/// AI-specific constants
namespace AI_Const
{

// More constants in:
//   AI_Movement::calc_avoidance()
//   AI_Drone::step() - IdleResource state

/// How often online check is performed
const TimeSpan online_check_timeout = TimeSpan::seconds(0.5);

/// How often group is updated (radio, area-of-sight)
const TimeSpan group_update_timeout = TimeSpan::seconds(0.5);

/// How often drone can call for help
const TimeSpan helpcall_timeout = TimeSpan::seconds(5);



/// Max distance between points considered same when using SlowPrecise (squared)
const float move_slowprecise_dist_squ = 0.5 * 0.5;

/// Distance to target at which it can be reset in Patrol speed mode (squared)
const float move_patrol_reset_distance_squ = 7 * 7;

/// Minimal distance at which 'rare_pos' and 'tmp_lock' are reset
const float move_lock_distance = 5;

/// Additional time for 'rare_pos' lock check
const TimeSpan move_lock_tolerance = TimeSpan::seconds(2);

/// Timeout before switching rotation types (fix for twitching)
const TimeSpan fixed_rotation_length = TimeSpan::seconds(0.3);



/// Time to wait on patrol point
const TimeSpan patrol_point_wait = TimeSpan::seconds(8);

// Special patrol check
const float patrol_raycast_width = 3;
const float patrol_raycast_length = 5;



/// Target lock length
//const TimeSpan target_switch_timeout = TimeSpan::seconds(1.5);

/// If distance between locked and nearest targets is bigger, re-locks to nearest
//const float target_switch_distance = 3;

/// Range in which target is detected out of FoV (squared)
const float target_hearing_range_squ = 4*4;

/// Extended range in which target is detected out of FoV (squared)
const float target_hearing_ext_range_squ = 12*12;

/// Target speed threshold to switch between normal and extended range (squared)
const float target_hearing_ext_spd_threshold_squ = 15*15;



/// If target not seen that long, adjustment and first-shot delay are reset
const TimeSpan attack_reset_timeout = TimeSpan::seconds(1.5);

/// Delay before shooting target when first seen
const TimeSpan attack_first_shot_delay = TimeSpan::seconds(0.7);

/// Time to fully update target velocity accumulator
const TimeSpan attack_acc_adjust_time = TimeSpan::seconds(0.3);

// Attack correction TTR deviations - min and max
const float attack_ttr_dev0 = 0.1;
const float attack_ttr_dev1 = 0.25;

/// Fire LoS raycast width
const float attack_los_hwidth = 0.3;

/// How long AI attacks if hit by target out of range
const TimeSpan retaliation_length = TimeSpan::seconds(0.3);



/// Neighbour cells checked for crowding
const vec2i placement_crowd_dirs[] =
{
                       {0, -2},
             {-1, -1}, {0, -1}, {1, -1},
    {-2, 0}, {-1,  0},          {1,  0}, {2, 0},
             {-1,  1}, {0,  1}, {1,  1},
                       {0,  2}
};

/// When going to AoS placement - how far away from target should be
const float placement_follow_evade_radius = 20;

/// When going to AoS placement - how much cost added in pathfinding
const float placement_follow_evade_cost = 20;

/// Partially closed cells with higher priority difference are ignored
const int placement_max_prio_diff = 20;



/// Additional distance to target, to move bit more forward
const float chase_ahead_dist = 7;

/// Chase delay increase time, 0 to 1
const TimeSpan chase_wait_incr = TimeSpan::seconds(5);

/// Max chase delay (decrease time, 1 to 0)
const TimeSpan chase_wait_decr = TimeSpan::seconds(1);

/// Delay before starting search on unsuccessful chase
const TimeSpan chase_search_delay = TimeSpan::seconds(1);



/// Suspicion increase time, 0 to 1, if outside visible range
const TimeSpan suspect_incr = TimeSpan::seconds(3);

/// Suspicion increase time, 0 to 1, if inside visible range
const TimeSpan suspect_incr_close = TimeSpan::seconds(1.5);

/// Suspicion decrease time, 1 to 0
const TimeSpan suspect_decr = TimeSpan::seconds(6);

/// Suspicion threshold above which start chase
const float suspect_chase_thr = TimeSpan::seconds(1) / suspect_incr;

/// Suspicion set after losing engagement target
const float suspect_max_level = 1.5;

/// Initial level (set on seen)
const float suspect_initial = TimeSpan::seconds(1) / suspect_decr;

/// Minimal level after receiving damage
const float suspect_on_damage = suspect_chase_thr;



// Message distances (in rooms)
const int msg_engage_dist = 4;
const int msg_engage_relay_dist = 0;
const int msg_helpcall_dist = 5;
const int msg_helpcall_highprio_dist = 8;

/// Engage message sent only if group has less bots
const int msg_engage_max_bots = 18;



/// Time to wait on intermediate points when searching
const TimeSpan search_point_wait = TimeSpan::seconds(4);

/// Time to wait on last point when searching
const TimeSpan search_point_wait_last = TimeSpan::seconds(8);

/// Search rings distances
const std::array<int, 2> search_ring_dist = {8, 24};
}

#endif // AI_COMMON_HPP
