#ifndef AI_LOGIC_HPP
#define AI_LOGIC_HPP

#include <variant>
#include "entity.hpp"
#include "level_ctr.hpp"
#include "physics.hpp"
#include "weapon.hpp"



struct AI_NetworkGroup
{
	struct TargetInfo
	{
		EntityIndex eid;
		vec2fp pos;
		uint32_t seen_at; ///< When last seen, GameCore steps
		bool is_visible; ///< Set if visible now
	};
	
	
	void post_target(Entity* ent); ///< Target is visible
	void reset_target(vec2fp self_pos); ///< Target isn't near this position
	std::optional<TargetInfo> last_target(); ///< Newest info on target
	
private:
	std::optional<TargetInfo> tar;
};



struct AI_TargetProvider : EComp
{
	struct NoTarget  {};
	struct LastKnown {vec2fp pos;}; ///< Target was seen at this pos
	struct Visible   {Entity* ent; float dist;}; ///< Target is directly visible
	using Info = std::variant<NoTarget, LastKnown, Visible>;
	
	std::shared_ptr<AI_NetworkGroup> group;
	
	
	AI_TargetProvider(Entity* ent, std::shared_ptr<AI_NetworkGroup> group = {});
	const Info& get_info() const {return info;}
	
protected:
	EVS_SUBSCR;
	
	struct InfoInternal
	{
		Entity* ent = nullptr;
		float dist = 0;
	};
	virtual InfoInternal update() = 0; ///< Called once per step, returns visible target
	
private:
	Info info;
	
	void step() override;
	void on_dmg(const DamageQuant& q);
};



struct AI_Attack
{
	bool use_prediction = true;
	
	
	void shoot(AI_TargetProvider::Visible info, Entity* self);
	void shoot(vec2fp at, Entity* self);
	
private:
	EntityIndex prev_tar;
	vec2fp vel_acc = {};
	
	vec2fp correction(float distance, float bullet_speed, Entity* target);
};



struct AI_Movement : EComp
{
	enum SpeedType
	{
		SPEED_SLOW,
		SPEED_NORMAL,
		SPEED_ACCEL,
		
		TOTAL_COUNT_INTERNAL
	};
	
	AI_Movement(Entity* ent, float spd_slow, float spd_norm, float spd_accel); ///< Registers
	void set_target(std::optional<vec2fp> new_tar, SpeedType speed = SPEED_NORMAL);
	
private:
	const std::array<float, TOTAL_COUNT_INTERNAL> spd_k;
	const std::array<float, TOTAL_COUNT_INTERNAL> inert_k = {6, 4, 8};
	
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
	
	float evade_rad;
	size_t evade_index = 0;
	
	
	vec2fp step_path();
	void step() override;
	b2Vec2 get_evade();
	b2Vec2 get_evade_vel(b2Vec2 vel);
};



struct AI_DroneLogic : EComp
{
	AI_TargetProvider* tar; ///< Must be non-null
	AI_Movement* mov; ///< May be null
	
	AI_Attack atk;
	float min_dist = 0; ///< Closest optimal distance
	float opt_dist = 100; ///< Farthest optimal distance
	
	AI_DroneLogic(Entity* ent, AI_TargetProvider* tar, AI_Movement* mov);
	
private:
	TimeSpan seen_tmo = TimeSpan::seconds(1000);
	vec2fp seen_pos;
	
	void step() override;
};



struct AI_TargetPlayer : AI_TargetProvider
{
	AI_TargetPlayer(Entity* ent, float vis_rad, std::shared_ptr<AI_NetworkGroup> grp);
	
private:
	float vis_rad;
	
	InfoInternal update() override;
};



struct AI_TargetSensor : AI_TargetProvider
{
	struct FI_Sensor : FixtureInfo {};
	
	TimeSpan chx_tmo = TimeSpan::seconds(1.5); ///< Target lock length
	float chx_dist = 3; ///< If distance between locked and nearest targets is bigger, re-locks to nearest
	
	AI_TargetSensor(EC_Physics& ph, float vis_rad);
	~AI_TargetSensor();
	
private:
	float vis_rad;
	b2Fixture* fix;
	
	std::vector<EntityIndex> tars;
	EntityIndex prev_tar; ///< locked target
	TimeSpan tmo;
	
	InfoInternal update() override;
	void on_cnt(const CollisionEvent& ev);
};

#endif // AI_LOGIC_HPP
