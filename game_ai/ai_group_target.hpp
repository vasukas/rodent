#ifndef AI_GROUP_TARGET_HPP
#define AI_GROUP_TARGET_HPP

#include "ai_common.hpp"



/// Area-of-sight helper struct
struct AI_AOS
{
	struct FrontRay {
		vec2i cell;
		uint8_t i0, i1; ///< Origin indices range, [i0, i1). Can be wrapped: i0 > i1 (only first ray of first front)
		float dist = 0; ///< Real distance (approx), NOT squared
	};
	struct Front {
		bool closer; ///< If set, rays don't reach ring distance
		std::vector<FrontRay> rays;
	};
	struct Ring {
		float dist; ///< Squared
		float perlen; ///< Perimeter length
		std::vector<Front> fronts;
	};
	
	size_t origs_size; ///< Number of original rays
	std::vector<Ring> rings;
	
	vec2fp origin_pos;
	float ray_diff; ///< Distance between adjacent rays in radians
	
	//
	
	/// ring_dist must be ascending-order set
	void generate(vec2fp origin, const std::vector<float>& ring_dist);
	
	/// Returns next world position in that direction - from [-pi, +pi] angle and square of current distance
	vec2fp get_next(float angle, float dist_squ, bool Left_or_right);
	
private:
	bool is_in(Front&    f, uint8_t opt_index);
	bool is_in(FrontRay& r, uint8_t opt_index);
};



struct AI_GroupTarget
{
	EntityIndex eid;
	vec2fp last_pos; ///< Position when last seen
	TimeSpan last_seen; ///< GameCore time
	bool is_lost = false; ///< True if not on last position
	
	bool aos_drone_change = false; ///< Set if update required due to drones/groups added/removed
	
	//
	
	bool report(); ///< Visible now. Returns true if position NOT changed much
	bool is_visible() const; ///< Or was until not long ago
	
	std::vector<std::vector<vec2fp>> build_search(Rect grid_area) const; ///< Calculates search rings
	AI_AOS* get_current_aos(); ///< Returns null if it's not available yet
	
private:
	std::vector<AI_Group*> groups; ///< Groups sharing this target
	
	AI_AOS aos;
	std::vector<float> aos_dist; ///< Ring distancies
	
	TimeSpan aos_time; ///< GameCore time of last AoS update
	vec2i aos_prev = {-1, -1}; ///< Previous grid coordinates
	bool aos_request = false;
	
	//
	
	friend class AI_Controller_Impl;
	
	void update_aos(); ///< Rebuilds AoS if needed
	
	void ref(AI_Group* g);
	void unref(AI_Group* g);
};

#endif // AI_GROUP_TARGET_HPP
