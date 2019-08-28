#ifndef S_OBJS_HPP
#define S_OBJS_HPP

#include "client/presenter.hpp"
#include "damage.hpp"
#include "physics.hpp"


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

#endif // S_OBJS_HPP
