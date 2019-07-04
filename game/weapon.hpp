#ifndef WEAPON_HPP
#define WEAPON_HPP

#include "vaslib/vas_time.hpp"
#include "damage.hpp"

struct ContactEvent;



struct Projectile : EComp
{
	enum Type
	{
		T_BULLET, ///< Only direct hit
		T_AOE, ///< Explosion
//		T_RAY ///< Direct hit ray (does not produce projectile object)
	};
	struct Params
	{
		DamageQuant dq = {DamageType::Kinetic, 0.f};
		Type type = T_BULLET;
		
		float rad = 1.f; ///< Area-of-effect radius
		float rad_min = 0.3f; ///< Minimal radius in damage calculation
		bool rad_full = false; ///< If true, radius ignored in damage calculation; otherwise linear fall-off is used
		
		float imp = 0.f; ///< Physical impulse applied to target
	};
	
	EVS_SUBSCR;
	Params pars;
	uint32_t src_eid = 0; ///< Source entity (may be 0)
	std::optional<Transform> target_pos; ///< Target for homing projectiles (NOT IMPLEMENTED)
	
	
	Projectile(Entity* ent);
	void on_event(const ContactEvent& ev);
};



struct Weapon
{
	Projectile::Params pars;
	TimeSpan shoot_delay = TimeSpan::seconds(0.5);
	
	bool is_homing = false; ///< If true, projectiles are homing onto target (NOT IMPLEMENTED)
	float proj_spd = 20.f; ///< Per second
	float proj_radius = 0.2f;
	size_t proj_sprite = 0;
	
	float heat_on  = 1; ///< Overheating enabled when higher
	float heat_off = 0.5; ///< Overheating disabled when lower
	float heat_incr = 0.3; ///< Overheat increase per second of shooting
	float heat_decr = 0.5; ///< Overheat decrease per second of non-shooting
	
	size_t ren_id = size_t_inval; ///< Rendering object ID
	Transform ren_tr = {}; ///< Rendering transform relative to parent
	
	bool needs_ammo = false; ///< If true, consumes ammo
	float ammo_speed = 0.f; ///< Ammo per second of shooting
	float ammo = 1.f; ///< Current ammo value
	
	
	
	Weapon() {reset();}
	void step();
	
	void shoot(Transform from, Transform at, Entity* src = nullptr); ///< Does nothing if can't yet shoot
	void shoot(Entity* parent, std::optional<Transform> at = {}); ///< Gets transforms from parent
	
	void reset(); ///< Resets counters (except ammo)
	
	bool can_shoot() const; ///< Returns true if capable of shooting now
	
private:
	TimeSpan del_cou; ///< Delay counter
	float heat_cou; ///< Current heat value
	bool heat_flag; ///< Is overheated
	bool shot_was; ///< Is shooting ray now
};



struct EC_Equipment : EComp
{
	std::vector<Weapon> wpns;
	
	
	EC_Equipment();
	~EC_Equipment();
	void step();
	
	bool set_wpn(size_t index); ///< Returns false if can't
	
	Weapon* wpn_ptr(); ///< Returns current weapon (or nullptr if none)
	Weapon& get_wpn(); ///< Returns current weapon (throws if none)
	
private:
	size_t wpn_cur = size_t_inval;
	size_t wpn_ren_id = size_t_inval;
};

#endif // WEAPON_HPP
