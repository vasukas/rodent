#ifndef EFFECTS_HPP
#define EFFECTS_HPP

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

#endif // EFFECTS_HPP
