#ifndef OBJS_CREATURE_HPP
#define OBJS_CREATURE_HPP

#include "client/presenter.hpp"
#include "game/physics.hpp"
#include "game/weapon.hpp"
#include "game_ai/ai_drone.hpp"

struct LaserDesigRay;



struct AtkPat_Sniper : AI_AttackPattern
{
	AtkPat_Sniper();
	~AtkPat_Sniper();
	void shoot(Entity& target, float distance, Entity& self) override;
	void idle(Entity& self) override;
	void reset(Entity& self) override;
	
private:
	LaserDesigRay* laser;
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



class ETurret final : public Entity
{
	EC_Physics phy;
	EC_RenderBot ren;
	EC_Health hlc;
	EC_Equipment eqp;
	AI_Drone logic;
	size_t team;
	
public:
	ETurret(vec2fp at, size_t team);
	std::string ui_descr() const override {return "Turret";}
	float get_face_rot() override {return logic.face_rot;}
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
	EC_Health*    get_hlc() override {return &hlc;}
	EC_Equipment* get_eqp() override {return &eqp;}
	size_t get_team() const override {return team;}
	AI_Drone* get_ai_drone() override {return &logic;}
};



class EEnemyDrone final : public Entity
{
	EC_Physics phy;
	EC_RenderBot ren;
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
		Weapon* wpn; // owning
		AI_AttackPattern* atk_pat = {}; // owning
		float drop_value = 0;
		bool is_worker = false;
		std::vector<vec2fp> patrol = {};
	};
	
	EEnemyDrone(vec2fp at, Init init);
	~EEnemyDrone();
	std::string ui_descr() const override {return "Drone";}
	float get_face_rot() override {return logic.face_rot;}
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
	EC_Health*    get_hlc() override {return &hlc;}
	EC_Equipment* get_eqp() override {return &eqp;}
	size_t get_team() const override {return TEAM_BOTS;}
	AI_Drone* get_ai_drone() override {return &logic;}
};

#endif // OBJS_CREATURE_HPP
