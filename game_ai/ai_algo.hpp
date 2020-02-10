#ifndef AI_ALGO_HPP
#define AI_ALGO_HPP

#include "game/level_ctr.hpp"
#include "game/physics.hpp"
struct AI_DroneParams;

/// Floodfills rooms, visiting each one only once, up to (excluding) max_depth. 
/// Function receives room and depth (starting with 0 for initial room). 
/// If function returns false, flood is stopped. 
/// If random_dirs is true, order of flood expansion is changed at each step, using GameCore random
void room_flood(vec2i pos, int max_depth, bool random_dirs, callable_ref<bool(const LevelControl::Room&, int)> f);
void room_flood_p(vec2fp pos, int max_depth, bool random_dirs, callable_ref<bool(const LevelControl::Room&, int)> f);

/// Performs physics query over room, limited by AI online range
void room_query(const LevelControl::Room& rm, callable_ref<bool(AI_Drone&)> f);

/// Performs physics query over circle
void area_query(vec2fp ctr, float radius, callable_ref<bool(AI_Drone&)> f);

/// Floodfilled rings
std::vector<std::vector<vec2fp>> calc_search_rings(vec2fp ctr_pos);

/// Area-of-sight distribution
class AI_AOS
{
public:
	static AI_AOS* create();
	virtual ~AI_AOS() = default;
	
	virtual void place_begin(vec2fp origin, float max_radius) = 0;
	virtual void place_end() = 0;
	
	struct Placement {
		std::optional<vec2fp> tar;
	};
	struct PlaceParam {
		vec2fp at;
		const AI_DroneParams* dpar;
		bool is_static; ///< can't be moved
		bool is_visible; ///< true if sees target
	};
	virtual void place_feed(const PlaceParam& pars) = 0;
	virtual Placement place_result() = 0;
	
	virtual void debug_draw() = 0;
};

#endif // AI_ALGO_HPP
