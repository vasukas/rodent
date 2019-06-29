#ifndef DAMAGE_HPP
#define DAMAGE_HPP

#include "utils/ev_signal.hpp"
#include "entity.hpp"

struct ContactEvent;
struct EC_Physics;



enum class DamageType
{
	Direct, ///< Ignoring all coefficients and thresholds
	Physical
};



struct DamageEvent
{
	float amount;
};



struct EC_Health : EComp
{
	EVS_SUBSCR;
	
	float hp = 1.f, hp_max = 1.f;
	bool invincible = false;
	
	float ph_k = 0.5; ///< Impulse to damage coeff
	float ph_thr = 40.f; ///< Minimal physical impulse
	
	ev_signal<DamageEvent> on_damage; ///< Signalled after damage calculation
	
	
	
	EC_Health() = default;
	
	void renew_hp(float max) {hp = hp_max = max;}
	void damage(DamageType type, float value);
	
	void hook(EC_Physics& ph); ///< Connects to collision event
	void on_event(const ContactEvent& ev);
};

#endif // DAMAGE_HPP
