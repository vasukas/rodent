#ifndef AI_LOGIC_HPP
#define AI_LOGIC_HPP

#include "entity.hpp"
#include "level_ctr.hpp"
#include "physics.hpp"



struct AI_TargetPredictor
{
	bool enabled = true; ///< Not used internally
	
	vec2fp correction(float distance, float bullet_speed, Entity* target); ///< Returns relative correction
};



struct AI_TargetProvider
{
	struct TargetInfo
	{
		Entity* ent; ///< Null if not visible
		float dist; ///< Distance to target (may be set to zero if not visible)
		std::optional<vec2fp> pos; ///< Position to find target
	};
	
	virtual ~AI_TargetProvider() = default;
	virtual void step() {}
	
	virtual TargetInfo get_target() const = 0;
	
protected:
	void check_lpos(std::optional<vec2fp>& lpos, Entity* self);
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
	bool has_target() const {return preq || path;}
	
private:
	const std::array<float, TOTAL_COUNT_INTERNAL> spd_k;
	const std::array<float, TOTAL_COUNT_INTERNAL> inert_k = {2, 1, 2};
	
	const float near_dist = LevelControl::get().cell_size * 2;
	const float reach_dist = LevelControl::get().cell_size * 0.6;
	
	std::optional<std::pair<vec2fp, PathRequest>> preq; ///< Pending request and endpoint
	std::optional<std::vector<vec2fp>> path;
	std::optional<vec2fp> path_fixed_dir;
	size_t path_index; ///< Next point, valid only if 'path' is
	
	size_t cur_spd = 1;
	
	vec2fp step_path();
	void step() override;
};



struct AI_DroneLogic : EComp
{
	std::unique_ptr<AI_TargetProvider> tar; ///< Must be non-null
	AI_Movement* mov; ///< May be null
	
	AI_TargetPredictor tar_pred;
	float min_dist = 0; ///< Closest optimal distance
	float opt_dist = 100; ///< Farthest optimal distance
	
	AI_DroneLogic(Entity* ent, std::unique_ptr<AI_TargetProvider>, AI_Movement* mov);
	
private:
	void step() override;
};



struct AI_TargetPlayer : AI_TargetProvider
{
	AI_TargetPlayer(Entity* ent, float vis_rad);
	void step() override;
	TargetInfo get_target() const override {return ti;}
	
private:
	Entity* ent;
	float vis_rad;
	
	TargetInfo ti = {};
};



struct AI_TargetSensor : AI_TargetProvider
{
	struct FI_Sensor : FixtureInfo {};
	
	TimeSpan chx_tmo = TimeSpan::seconds(1.5); ///< Target lock length
	float chx_dist = 3; ///< If distance between locked and nearest targets is bigger, re-locks to nearest
	
	AI_TargetSensor(EC_Physics& ph, float vis_rad);
	~AI_TargetSensor();
	void step() override;
	TargetInfo get_target() const override {return ti;}
	
private:
	EVS_SUBSCR;
	
	Entity* ent;
	float vis_rad;
	b2Fixture* fix;
	
	std::vector<EntityIndex> tars;
	EntityIndex prev_tar; ///< locked target
	TimeSpan tmo;
	TargetInfo ti = {};
	
	void on_cnt(const CollisionEvent& ev);
};



struct AI_TargetNetwork : AI_TargetProvider
{
	struct Group
	{
		std::vector<AI_TargetNetwork*> nodes;
		std::optional<vec2fp> get_pos();
	};
	
	TimeSpan ask_tmo = TimeSpan::seconds(1);
	
	AI_TargetNetwork(Entity* ent, std::shared_ptr<Group> grp, std::unique_ptr<AI_TargetProvider> prov);
	~AI_TargetNetwork();
	
	void step() override;
	TargetInfo get_target() const override {return ti;}
	
private:
	Entity* ent;
	std::shared_ptr<Group> grp;
	std::unique_ptr<AI_TargetProvider> prov;
	
	TimeSpan tmo;
	TargetInfo ti = {};
	std::optional<vec2fp> net_pos;
};

#endif // AI_LOGIC_HPP
