#ifndef OBJS_CREATURE_HPP
#define OBJS_CREATURE_HPP

#include "client/ec_render.hpp"
#include "game/physics.hpp"
#include "game/weapon.hpp"
#include "game_ai/ai_drone.hpp"

struct LaserDesigRay;



struct AtkPat_Sniper : AI_AttackPattern
{
	float rot_speed = deg_to_rad(10); // rotation speed (per second) when charged and targeted
	
	void shoot(Entity& target, float distance, Entity& self) override;
	void idle(Entity& self) override;
	void reset(Entity& self) override;
	
private:
	vec2fp p_tar;
	bool has_tar = false;
};

struct AtkPat_Burst : AI_AttackPattern
{
	struct Crowd {
		TimeSpan max_wait = TimeSpan::seconds(3);
		int max_bots = 8; ///< How many bots for 'max_wait'
		float radius = 8;
	};
	TimeSpan wait  = TimeSpan::seconds(0.5);
	TimeSpan burst = TimeSpan::seconds(1);
	std::optional<Crowd> crowd = Crowd{};
	
	void shoot(Entity& target, float distance, Entity& self) override;
	void idle(Entity& self) override;
	void reset(Entity& self) override;
	
private:
	TimeSpan crowd_tmo, tmo;
	bool shooting = false;
	int crowd_bots = 0;
};

struct AtkPat_Boss : AI_AttackPattern
{
	struct Stage {
		TimeSpan len; ///< Attack length
		TimeSpan wait; ///< Pause after attack
		
		size_t i_wpn = 0;
		bool continious = true; ///< Attack even if target isn't visible
		bool targeted = false; ///< If true, shoots to target position even if not visible
		
		std::optional<float> rot_limit; ///< Rotation speed limit while attacking
		std::optional<float> opt_dist; ///< Optimal distance
	};
	std::vector<Stage> stages;
	
	void shoot(Entity& target, float distance, Entity& self) override;
	void idle(Entity& self) override;
	void reset(Entity& self) override;
	
private:
	static constexpr TimeSpan t_stop  = TimeSpan::seconds(1); // stop continious attack
	static constexpr TimeSpan t_reset = TimeSpan::seconds(8); // reset attack pattern
	size_t i_st = 0;
	bool pause = true;
	TimeSpan tmo, seen_at;
	
	void set_dist(Entity& self, std::optional<float> dist);
	bool passed(Entity& self, TimeSpan t);
	void set_stage(Entity& self, size_t i);
};



class ETurret final : public Entity
{
	EC_Physics phy;
	EC_Health hlc;
	EC_Equipment eqp;
	AI_Drone logic;
	size_t team;
	
public:
	ETurret(GameCore& core, vec2fp at, size_t team);
	~ETurret();
	
	EC_Position&   ref_pc() override  {return  phy;}
	EC_Health*     get_hlc() override {return &hlc;}
	EC_Equipment*  get_eqp() override {return &eqp;}
	AI_Drone* get_ai_drone() override {return &logic;}
	size_t        get_team() const override {return team;}
};



class EEnemyDrone final : public Entity
{
	EC_Physics phy;
	EC_Health hlc;
	EC_Equipment eqp;
	AI_Drone logic;
	AI_Movement mov;
	float drop_value;
	
public:
	struct Init
	{
		std::shared_ptr<AI_DroneParams> pars;
		ModelType model = MODEL_DRONE;
		FColor color = FColor(1, 0, 0, 1);
		std::unique_ptr<Weapon> wpn;
		std::unique_ptr<AI_AttackPattern> atk_pat; // optional
		float drop_value = 0;
		bool is_worker = false;
		bool is_accel = false;
		std::vector<vec2fp> patrol = {};
		int hp = 70;
		std::unique_ptr<DamageFilter> shield = {};
	};
	
	static Init def_workr(GameCore& core);
	static Init def_drone(GameCore& core);
	static Init def_campr(GameCore& core);
	static Init def_fastb(GameCore& core);
	
	EEnemyDrone(GameCore& core, vec2fp at, Init init);
	~EEnemyDrone();
	
	EC_Position&   ref_pc() override  {return  phy;}
	EC_Health*     get_hlc() override {return &hlc;}
	EC_Equipment*  get_eqp() override {return &eqp;}
	AI_Drone* get_ai_drone() override {return &logic;}
	size_t        get_team() const override {return TEAM_BOTS;}
};



class EHunter final : public Entity
{
	EC_Physics phy;
	EC_Health hlc;
	EC_Equipment eqp;
	AI_Drone logic;
	AI_Movement mov;
	
public:
	EHunter(GameCore& core, vec2fp at);
	~EHunter();
	
	EC_Position&   ref_pc() override  {return  phy;}
	EC_Health*     get_hlc() override {return &hlc;}
	EC_Equipment*  get_eqp() override {return &eqp;}
	AI_Drone* get_ai_drone() override {return &logic;}
	size_t        get_team() const override {return TEAM_BOTS;}
};



class EHacker final : public Entity
{
	EC_Physics phy;
	EC_Health hlc;
	EC_Equipment eqp;
	AI_Drone logic;
	AI_Movement mov;
	
	void step() override;
	
public:
	EHacker(GameCore& core, vec2fp at);
	~EHacker();
	
	EC_Position&   ref_pc() override  {return  phy;}
	EC_Health*     get_hlc() override {return &hlc;}
	EC_Equipment*  get_eqp() override {return &eqp;}
	AI_Drone* get_ai_drone() override {return &logic;}
	size_t        get_team() const override {return TEAM_BOTS;}
};

#endif // OBJS_CREATURE_HPP
