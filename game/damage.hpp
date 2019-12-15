#ifndef DAMAGE_HPP
#define DAMAGE_HPP

#include "utils/ev_signal.hpp"
#include "entity.hpp"

struct CollisionEvent;
struct EC_Physics;

struct DamageFilter;



enum class DamageType
{
	Direct, ///< Ignoring armor, thresholds etc
	Physical,
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
	int regen_hp = 0; ///< Healing amount
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
	EVS_SUBSCR;
	ev_signal<DamageQuant> on_damage; ///< Contains original type and calculated damage (sent on damage >= 0)
	
	float ph_k = 0.5; ///< Impulse to damage coeff (collision damage)
	float ph_thr = 40.f; ///< Minimal physical impulse
	
	TimeSpan last_damaged; ///< GameCore time (set on damage >= 0)
	
	
	
	EC_Health(Entity* ent, int hp = 100);
	void apply(DamageQuant q);
	
	HealthPool& get_hp() {return hp;}
	void upd_hp(); ///< Must be called if regeneration params changed
	
	void hook(EC_Physics& ph); ///< Connects to collision event
	void on_event(const CollisionEvent& ev);
	
	size_t add_filter(std::shared_ptr<DamageFilter> f, std::optional<size_t> index = {}); ///< Adds filter
	void rem_filter(size_t i); ///< Removes protected area
	
	size_t add_prot(std::shared_ptr<DamageFilter> f, std::optional<size_t> index = {}); ///< Adds protected area
	void rem_prot(size_t i); ///< Removes protected area
	
	std::vector<std::shared_ptr<DamageFilter>>& raw_fils() {return fils;}
	std::vector<std::shared_ptr<DamageFilter>>& raw_prot() {return pr_area;}
	
private:
	HealthPool hp;
	std::vector<std::shared_ptr<DamageFilter>> fils; // may contain nullptr
	std::vector<std::shared_ptr<DamageFilter>> pr_area; // may contain nullptr
	
	size_t add(std::vector<std::shared_ptr<DamageFilter>>& fs, std::shared_ptr<DamageFilter> f, std::optional<size_t> index);
	void rem(std::vector<std::shared_ptr<DamageFilter>>& fs, size_t i);
	void step() override;
};



struct DamageFilter
{
	virtual ~DamageFilter() = default;
	virtual void proc(EC_Health&, DamageQuant& q) = 0;
	virtual void step(EC_Health&) {} ///< Called each step
	virtual bool has_step() const {return false;} ///< Must return true if step() is implemented
};



struct DmgShield : DamageFilter
{
	static constexpr int dead_absorb = 100; ///< How much additional damage absorbed on destruction
	bool enabled = true;
	bool is_filter = true; // used only for rendering
	
	DmgShield(int capacity, int regen_per_second, TimeSpan regen_wait = TimeSpan::seconds(3));
	void proc(EC_Health&, DamageQuant& q);
	void step(EC_Health&);
	bool has_step() const {return true;}
	
	HealthPool& get_hp() {return hp;}
	
private:
	HealthPool hp;
	TimeSpan hit_ren_tmo;
};



struct DmgArmor : DamageFilter
{
	// ignores non-kinetic damage!
	int thr = 5; ///< Dmg below this doesn't get through
	float mod = 0.5f; ///< Dmg mutiplier (after threshold)
	float self = 0.3f; ///< Dmg to armor multiplier (NOT after mod)
	
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
