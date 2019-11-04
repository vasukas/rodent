#ifndef S_OBJS_HPP
#define S_OBJS_HPP

#include "client/presenter.hpp"
#include "game_ai/ai_drone.hpp"
#include "level_ctr.hpp"
#include "physics.hpp"
#include "weapon.hpp"



class EWall final : public Entity
{
	EC_Physics phy;
	EC_RenderSimple ren;
	
public:
	EWall(const std::vector<std::vector<vec2fp>>& walls);
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
};



class EPhyBox final : public Entity
{
	EC_Physics phy;
	EC_RenderSimple ren;
	EC_Health hlc;
	
public:
	EPhyBox(vec2fp at);
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
	EC_Health*    get_hlc() override {return &hlc;}
};



/// Works only on player
class EPickable final : public Entity
{
public:
	struct AmmoPack
	{
		AmmoType type;
		float amount;
	};
	
	struct Func
	{
		/// Returns true if pickable should be destroyed
		std::function<bool(Entity&)> f;
		ModelType model;
	};
	
	using Value = std::variant<AmmoPack, Func>;
	
	
	
	EPickable(vec2fp pos, Value val);
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
	
private:
	EC_Physics phy;
	EC_RenderSimple ren;
	Value val;
	
	EVS_SUBSCR;
	void on_cnt(const CollisionEvent& ce);
};



class ETurret final : public Entity
{
	EC_Physics phy;
	EC_RenderBot ren;
	EC_Health hlc;
	EC_Equipment eqp;
	AI_Drone logic;
	AI_TargetSensor l_tar;
	size_t team;
	
public:
	ETurret(vec2fp at, std::shared_ptr<AI_Group> grp, size_t team);
	
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
	AI_TargetPlayer l_tar;
	AI_Movement mov;
	
public:
	struct Init
	{
		std::shared_ptr<AI_Group> grp;
		std::shared_ptr<AI_DroneParams> pars;
	};
	
	EEnemyDrone(vec2fp at, const Init& init);
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
	EC_Health*    get_hlc() override {return &hlc;}
	EC_Equipment* get_eqp() override {return &eqp;}
	size_t get_team() const override {return TEAM_BOTS;}
	AI_Drone* get_ai_drone() override {return &logic;}
};



class EDoor final : public Entity
{
	struct RenDoor : ECompRender {
		RenDoor(Entity* ent);
		void step() override;
	};
	
	struct FI_Sensor : FixtureInfo {};
	
	enum State {
		ST_OPEN,
		ST_CLOSED,
		ST_TO_OPEN,
		ST_TO_CLOSE
	};
	
	static constexpr float width = 0.5; // relative to cell size
	static constexpr float offset = 0.1; // relative to cell size
	static constexpr TimeSpan wait_time = TimeSpan::seconds(0.3); // before opening
	static constexpr TimeSpan anim_time = TimeSpan::seconds(0.5); // opening/closing time
	static constexpr TimeSpan keep_time = TimeSpan::seconds(7); // kept open
	static constexpr float min_off = 0.2; // meters; length in 'closed' state
	
	EC_Physics phy;
	RenDoor ren;
	
	TimeSpan tm_left;
	State state = ST_CLOSED;
	
	bool is_x_ext;
	vec2fp fix_he;
	b2Fixture* fix = nullptr;
	
	EVS_SUBSCR;
	void on_cnt(const CollisionEvent& ce);
	void open();
	
	void step() override;
	void upd_fix();
	
public:
	EDoor(vec2i TL_origin, vec2i door_ext, vec2i room_dir);
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
};

#endif // S_OBJS_HPP
