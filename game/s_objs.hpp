#ifndef S_OBJS_HPP
#define S_OBJS_HPP

#include <variant>
#include "client/presenter.hpp"
#include "ai_logic.hpp"
#include "level_ctr.hpp"
#include "physics.hpp"
#include "weapon.hpp"


class EWall final : public Entity
{
public:
	EC_Physics phy;
	EC_RenderSimple ren;
	
	EWall(const std::vector<std::vector<vec2fp>>& walls);
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
};


class EPhyBox final : public Entity
{
public:
	EC_Physics phy;
	EC_RenderSimple ren;
	EC_Health hlc;
	
	EPhyBox(vec2fp at);
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
	EC_Health*    get_hlc() override {return &hlc;}
};


class ESupply final : public Entity
{
public:
	struct AmmoPack
	{
		WeaponIndex type;
		float amount;
	};
	
	EC_Physics phy;
	EC_RenderSimple ren;
	std::variant<AmmoPack> val;
	
	ESupply(vec2fp pos, AmmoPack ap);
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
	
private:
	EVS_SUBSCR;
	void on_cnt(const CollisionEvent& ce);
};


class ETurret final : public Entity
{
public:
	EC_Physics phy;
	EC_RenderBot ren;
	EC_Health hlc;
	EC_Equipment eqp;
	AI_DroneLogic logic;
	size_t team;
	
	ETurret(vec2fp at, size_t team);
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
	EC_Health*    get_hlc() override {return &hlc;}
	EC_Equipment* get_eqp() override {return &eqp;}
	size_t get_team() const override {return team;}
};


class EEnemyDrone final : public Entity
{
public:
	EC_Physics phy;
	EC_RenderBot ren;
	EC_Health hlc;
	EC_Equipment eqp;
	AI_Movement mov;
	AI_DroneLogic logic;
	
	EEnemyDrone(vec2fp at);
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
	EC_Health*    get_hlc() override {return &hlc;}
	EC_Equipment* get_eqp() override {return &eqp;}
	size_t get_team() const override {return TEAM_BOTS;}
};

#endif // S_OBJS_HPP
