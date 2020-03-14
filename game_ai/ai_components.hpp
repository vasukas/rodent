#ifndef AI_COMPONENTS_HPP
#define AI_COMPONENTS_HPP

#include "game/damage.hpp"
#include "game/level_ctr.hpp"
#include "ai_common.hpp"

// Note: components are NOT registered on initialization



struct AI_Movement final : EComp
{
	bool locked = false;
	
	AI_Movement(AI_Drone& drone); ///< Adds self!
	
	/// Returns true if already reached
	bool set_target(std::optional<vec2fp> new_tar, AI_Speed speed = AI_Speed::Normal, std::optional<PathRequest::Evade> evade = {});
	bool has_target() const {return path || preq;}
	
	std::optional<vec2fp> get_next_point() const;
	float get_set_speed() const; ///< Returns current *set* speed, not actual one
	bool has_failed() const; ///< Returns true if target is unreachable
	
	bool is_same(vec2fp a, vec2fp b) const; ///< Checks if two coordinates refer to (roughly) same position
	std::optional<vec2fp> get_target() const; ///< Returns current target, if any
	
	void on_unreg();
	
private:
	AI_Drone& drone;
	
	struct Preq {
		vec2fp target;
		PathRequest req;
	};
	struct Path {
		std::vector<vec2fp> ps;
		size_t next; ///< Index into 'ps'
	};
	
	std::optional<Preq> preq;
	std::optional<Path> path;
	AI_Speed cur_spd = AI_Speed::Slow;
	bool preq_failed = false;
	std::optional<vec2fp> patrol_reset;
	
	vec2fp rare_pos = vec2fp::one(-1000);
	TimeSpan rare_last;
	LevelCtrTmpLock path_lock;
	
	
	std::optional<vec2fp> calc_avoidance(); // collision avoidance vector
	vec2fp step_path();
	void lock_check();
	void step() override;
	
	static float inert_k(AI_Speed speed);
};



struct AI_AttackPattern
{
	virtual void shoot(Entity& target, float distance, Entity& self) = 0;
	virtual void idle (Entity& self) {(void) self;} ///< Called on each step while in battle, after shooting
	virtual void reset(Entity& self) {(void) self;} ///< Called when dropping out of battle mode
	virtual ~AI_AttackPattern() = default;
};

struct AI_Attack
{
	std::unique_ptr<AI_AttackPattern> atkpat;
	
	AI_Attack(std::unique_ptr<AI_AttackPattern> atkpat): atkpat(std::move(atkpat)) {}
	bool shoot(Entity& target, float distance, Entity& self); ///< Returns false if fire LoS is obstructed
	void reset_target() {prev_tar = {};}
	
private:
	EntityIndex prev_tar;
	vec2fp vel_acc = {};
	
	vec2fp correction(float distance, float bullet_speed, Entity& target);
	Entity* check_los(vec2fp pos, Entity& target, Entity& self); ///< Returns obstructing entity, if any
};



struct AI_TargetProvider final : EComp
{
	struct Target
	{
		EntityIndex eid;
		bool is_suspect; ///< True if may not be target (out of main visibility range)
		bool is_damaging; ///< True if just inflicted damage
		float dist;
	};
	
	std::optional<float> fov_t = 0.f; ///< [0-1] FoV width (min -> max). If not set, FoV check ignored
	bool is_battle = false;
	
	AI_TargetProvider(AI_Drone& drone); ///< Adds self!
	
	/// Returns visible primary or suspect target (also returns distance)
	std::optional<Target> get_target() const;
	
	/// Returns true if was damaged last step
	bool was_damaged() const {return was_damaged_flag;}
	
protected:
	EVS_SUBSCR;
	AI_Drone& drone;
	
	bool is_primary(Entity& ent) const {return ent.is_creature();}
	
private:
	std::optional<Target> tar_sel;
	std::optional<EntityIndex> damage_by;
	bool was_damaged_flag = false;
	
	void step() override;
	void on_dmg(const DamageQuant& q);
};



struct AI_RotationControl
{
	std::optional<float> speed_override; ///< May be zero. For use by AI_AttackPattern
	
	void update(AI_Drone& dr, std::optional<vec2fp> view_target, std::optional<vec2fp> mov_target);
	
private:
	enum State {
		ST_WAIT,
		ST_RANDOM,
		ST_TAR_VIEW,
		ST_TAR_MOV,
		ST_WAIT_VIEW,
		ST_WAIT_MOV
	};
	float add; // radians/step
	TimeSpan tmo;
	State state = ST_WAIT;
};

#endif // AI_COMPONENTS_HPP
