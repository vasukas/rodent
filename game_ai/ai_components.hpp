#ifndef AI_COMPONENTS_HPP
#define AI_COMPONENTS_HPP

#include "game/physics.hpp"
#include "ai_common.hpp"

struct DamageQuant;

// Note: by default components are NOT registered



struct AI_Movement final : EComp
{
	vec2fp steering = {}; ///< Additional velocity, reset each step
	
	AI_Movement(AI_Drone* drone); ///< Adds self!
	
	/// Returns true if already reached
	bool set_target(std::optional<vec2fp> new_tar, AI_Speed speed = AI_Speed::Normal);
	bool has_target() const {return path || preq;}
	
	std::optional<vec2fp> get_next_point() const;
	float get_current_speed() const {return spd_k[cur_spd];} ///< Current *set* speed, not actual one
	
private:
	const float* spd_k;
	const float inert_k[static_cast<size_t>(AI_Speed::TOTAL_COUNT)] = {6, 4, 8};
	
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
	size_t cur_spd = 1;
	
	
	vec2fp step_path();
	void step() override;
};



struct AI_Attack
{
	bool shoot(Entity* target, float distance, Entity* self); ///< Returns false if fire LoS is obstructed
	void shoot(vec2fp at, Entity* self);
	
private:
	EntityIndex prev_tar;
	vec2fp vel_acc = {};
	
	vec2fp correction(float distance, float bullet_speed, Entity* target);
	Entity* check_los(vec2fp pos, Entity* target, Entity* self); ///< Returns obstructing entity, if any
};



struct AI_TargetProvider : EComp
{
	struct Target
	{
		EntityIndex eid;
		bool is_suspect; ///< True if may not be target
		float dist = 0;
	};
	struct ProjTarget
	{
		EntityIndex eid;
		float dist = 0;
	};
	
	AI_TargetProvider(AI_Drone* drone); ///< Adds self!
	
	/// Returns visible primary or suspect target (also returns distance)
	std::optional<Target> get_target() const;
	
	/// Returns "dangerous projectile" target
	std::optional<ProjTarget> get_projectile() const;
	
protected:
	EVS_SUBSCR;
	const AI_DroneParams& pars;
	std::vector<Target> tars; ///< All non-null non-projectile targets available
	std::vector<ProjTarget> proj_tars; ///< All non-null PROJECTILE targets available
	
	virtual void step_internal() = 0; ///< Updates available targets
	bool is_primary(Entity* ent) const {return ent->get_eqp();}
	
private:
	std::optional<Target> tar_sel;
	std::optional<ProjTarget> tar_proj;
	TimeSpan sw_timeout; ///< Target switch
	EntityIndex prev_tar;
	EntityIndex damage_by;
	
	void step() override;
	void on_dmg(const DamageQuant& q);
};



struct AI_TargetPlayer : AI_TargetProvider
{
	AI_TargetPlayer(AI_Drone* drone);
	
private:
	void step_internal() override;
};



struct AI_TargetSensor : AI_TargetProvider
{
	struct FI_Sensor : FixtureInfo {};
	
	AI_TargetSensor(AI_Drone* drone);
	~AI_TargetSensor();
	
private:
	b2Fixture* fix;
	
	void step_internal() override;
	void on_cnt(const CollisionEvent& ev);
};



struct AI_RenRotation
{
	AI_RenRotation();
	void update(Entity* ent, std::optional<vec2fp> target_pos, std::optional<vec2fp> move_pos);
	
private:
	enum State {RR_NONE, RR_STOP, RR_ADD};
	float rot;
	float add; // radians/step
	float left = 0; // seconds
	State st = RR_NONE;
};

#endif // AI_COMPONENTS_HPP
