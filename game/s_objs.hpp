#ifndef S_OBJS_HPP
#define S_OBJS_HPP

#include "client/presenter.hpp"
#include "damage.hpp"
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


class ETurret final : public Entity
{
public:
	struct Logic : EComp
	{
		std::vector<EntityIndex> tars;
		EVS_SUBSCR;
		
		TimeSpan rr_left;
		float rr_val = 0;
		bool rr_flag = false;
		
		Logic(Entity*);
		void step() override;
		void on_cnt(const ContactEvent& ev);
	};
	
	EC_Physics phy;
	EC_RenderBot ren;
	EC_Health hlc;
	EC_Equipment eqp;
	size_t team;
	Logic logic;
	
	ETurret(vec2fp at, size_t team);
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
	EC_Health*    get_hlc() override {return &hlc;}
	EC_Equipment* get_eqp() override {return &eqp;}
	size_t get_team() const override {return team;}
};

#endif // S_OBJS_HPP
