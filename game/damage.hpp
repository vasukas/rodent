#ifndef DAMAGE_HPP
#define DAMAGE_HPP

#include "utils/ev_signal.hpp"
#include "entity.hpp"
#include "physics.hpp"

struct DamageFilter;



enum class DamageType
{
	Direct, ///< Ignoring armor, thresholds etc
	Kinetic ///< Projectile weapon
};



struct DamageQuant
{
	DamageType type = DamageType::Kinetic;
	int amount = 0; ///< Negative is treated like zero
	std::optional<size_t> armor = {}; ///< Armored area index
	std::optional<vec2fp> wpos = {}; ///< World position (to display particles)
	EntityIndex src_eid = {}; ///< Who caused damage. If set, MUST be valid!
};



struct HealthPool
{
	// Note: regen works even if not alive
	int regen_hp = 0; ///< Healing amount (Note: must always be either zero or non-zero)
	TimeSpan regen_cd = TimeSpan::seconds(0.5); ///< Cooldown after healing
	TimeSpan regen_wait = TimeSpan::seconds(3); ///< Cooldown after receiving damage
	float regen_at = 1.f; ///< Amount hp below which regen is enabled
	
	
	HealthPool(int hp, std::optional<int> hp_max = {});
	
	bool is_alive() const {return hp > 0;}
	bool has_regen() const {return regen_hp != 0;}
	
	float t_state() const; ///< Returns hp value in range [0; 1]
	std::pair<int, int> exact() const {return {hp, hp_max};}
	
	void apply(int amount, bool limited = true);
	void renew(std::optional<int> new_max = {}); ///< Sets current hp to max
	
	void set_hps(int hps); ///< Sets healing per second with half-second cooldown
	void step(); ///< Regen hp if needed
	
private:
	int hp, hp_max;
	TimeSpan tmo; ///< Regeneration timeout
};



struct EC_Health : EComp
{
	ev_signal<DamageQuant> on_damage; ///< Contains original type and calculated damage (sent on damage >= 0)
	
	
	EC_Health(Entity& ent, int hp);
	bool apply(DamageQuant q); ///< Returns false if entity was killed, true otherwise
	
	HealthPool& get_hp() {return hp;}
	void upd_reg(); ///< Call this if regeneration params of HealthPool are changed
	
	TimeSpan since_damaged() const; ///< Returns time since last damage (reset even on zero damage)
	
	void add_phys(std::unique_ptr<DamageFilter> f);
	void add_filter(std::unique_ptr<DamageFilter> f); ///< Stack: last added is first processed
	
	void rem_phys(DamageFilter* f) noexcept;
	void rem_filter(DamageFilter* f) noexcept;
	
	size_t get_phys_index(DamageFilter* f) const; ///< Returns index of filter registered as phys
	void foreach_filter(callable_ref<void(DamageFilter&)> f); ///< Gets called for both types of filters
	
private:
	HealthPool hp;
	TimeSpan last_damaged; ///< GameCore time (set on damage >= 0)
	
	std::vector<std::unique_ptr<DamageFilter>> phys;
	std::vector<std::unique_ptr<DamageFilter>> fils;
	
	void step() override;
};



struct DamageFilter
{
	virtual ~DamageFilter() = default;
	virtual void proc(EC_Health&, DamageQuant& q) = 0;
	virtual void step(EC_Health&) {} ///< Called each step
	virtual bool has_step() const {return false;} ///< Must return true if implements step
};



struct DmgShield : DamageFilter
{
	/// Note: if fc is supplied (physics filter), shield is disabled by default
	DmgShield(int capacity, int regen_per_second,
	          TimeSpan regen_wait = TimeSpan::seconds(3),
	          std::optional<FixtureCreate> fc = {});
	
	HealthPool& get_hp() {return hp;}
	bool is_enabled() const {return enabled;}
	bool is_phys() const {return !!fix;}
	
	void set_enabled(Entity& ent, bool on);
	
private:
	bool enabled;
	HealthPool hp;
	TimeSpan hit_ren_tmo;
	std::optional<SwitchableFixture> fix;
	
	void proc(EC_Health&, DamageQuant& q);
	void step(EC_Health&);
	bool has_step() const {return true;}
};



struct DmgArmor : DamageFilter
{
	// Only for kinetic damage
	float k_self = 0.15; ///< Damage to armor multiplier (before mod)
	float k_mod_min = 0.7; ///< Received damage multiplier (min health)
	float k_mod_max = 0.2; ///< Received damage multiplier (full health)
//	int dmg_thr = 5; ///< If damage below this, it's ignored (after mod)
	
	DmgArmor(int hp_max, int hp = 0);
	void proc(EC_Health&, DamageQuant& q);
	HealthPool& get_hp() {return hp;}
	
private:
	HealthPool hp;
};



struct DmgIDDQD : DamageFilter
{
	void proc(EC_Health&, DamageQuant& q) {q.amount = 0;}
};

#endif // DAMAGE_HPP
