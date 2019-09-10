#ifndef DAMAGE_HPP
#define DAMAGE_HPP

#include "utils/ev_signal.hpp"
#include "entity.hpp"

struct ContactEvent;
struct EC_Physics;



enum class DamageType
{
	Direct, ///< Ignoring armor, thresholds etc
	Physical,
	Kinetic ///< Projectile weapon
};



struct DamageQuant
{
	DamageType type;
	int amount;
};



struct EC_Health : EComp
{
	EVS_SUBSCR;
	ev_signal<DamageQuant> on_damage; ///< Contains original type and final damage
	
	bool invincible = false; ///< Ignores all damage
	float ph_k = 0.5; ///< Impulse to damage coeff
	float ph_thr = 40.f; ///< Minimal physical impulse
	
	
	
	EC_Health(Entity* ent, int hp = 100.f);
	
	bool is_alive() const;
	float get_t_state() const; ///< Returns 0-1 current hp value
	
	void add_hp(int amount);
	void renew_hp(std::optional<int> new_max); ///< Sets current hp to max
	
	void apply(DamageQuant q, std::optional<DamageType> calc_type = {});
	
	void hook(EC_Physics& ph); ///< Connects to collision event
	void on_event(const ContactEvent& ev);
	
private:
	int hp = 1, hp_max = 100;
};

#endif // DAMAGE_HPP
