#ifndef EFFECTS_HPP
#define EFFECTS_HPP

#include "game/entity.hpp"
#include "utils/color_manip.hpp"
#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"



enum class EffectLightning
{
	First, ///< First in chain of branching (additional branches)
	Regular, ///< Branching
	Straight ///< Just single straight line
};

/// Creates running lightning bolt effect
void effect_lightning(vec2fp a, vec2fp b, EffectLightning type, TimeSpan length, FColor clr = FColor(0.6, 0.85, 1));



/// Creates concentric wave effect
void effect_explosion_wave(vec2fp ctr, float power = 1);



struct LaserDesigRay
{
	EntityIndex src_eid;
	FColor clr = FColor(1, 0, 0, 0.6);
	bool is_enabled = false;
	
	static LaserDesigRay* create();
	void destroy();
	
	void set_target(vec2fp new_tar);
	void find_target(vec2fp dir); ///< Performs raycast
	
private:
	bool to_delete = false;
	vec2fp tar_ray = {};
	std::optional<std::pair<vec2fp, vec2fp>> tar_next;
	float tar_next_t;
	
	void render(TimeSpan passed);
	LaserDesigRay()  = default;
	~LaserDesigRay() = default;
};

#endif // EFFECTS_HPP
