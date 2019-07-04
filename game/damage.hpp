#ifndef DAMAGE_HPP
#define DAMAGE_HPP

#include "utils/ev_signal.hpp"
#include "entity.hpp"

struct ContactEvent;
struct EC_Physics;



enum class DamageType
{
	Direct, ///< Ignoring all coefficients and thresholds
	Physical,
	Kinetic ///< Projectile weapon
};



struct DamageQuant
{
	DamageType type;
	float amount;
};



struct EC_Health : EComp
{
	EVS_SUBSCR;
	
	float hp = 1.f, hp_max = 1.f;
	bool invincible = false;
	
	float ph_k = 0.5; ///< Impulse to damage coeff
	float ph_thr = 40.f; ///< Minimal physical impulse
	
	ev_signal<DamageQuant> on_damage; ///< Contains original type and final damage
	
	
	
	EC_Health() = default;
	
	void renew_hp(float max) {hp = hp_max = max;}
	void damage(DamageQuant q, std::optional<DamageType> calc_type = {});
	
	void hook(EC_Physics& ph); ///< Connects to collision event
	void on_event(const ContactEvent& ev);
};

#endif // DAMAGE_HPP
