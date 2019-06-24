#include "entity.hpp"
#include "game_core.hpp"
#include "logic.hpp"
#include "movement.hpp"
#include "physics.hpp"
#include "presenter.hpp"



Entity::Entity( GameCore& core, EntityIndex index ) : index( index ), core( core ) {}
Entity::~Entity() = default;
void Entity::destroy() {core.mark_deleted(this);}



Transform Entity::get_pos() const
{
	if (c_phy) return conv(c_phy->body->GetTransform());
	if (setpos) return setpos->pos;
	return {};
}
Transform Entity::get_vel() const
{
	if (c_phy) return {conv(c_phy->body->GetLinearVelocity()), c_phy->body->GetAngularVelocity()};
	if (setpos) return setpos->vel;
	return {};
}
vec2fp Entity::get_norm_dir() const
{
	vec2fp p{1, 0};
	p.fastrotate( get_pos().rot );
	return p;
}
float Entity::get_radius() const
{
	if (c_phy) return c_phy->b_radius;
	if (setpos) return setpos->radius;
	return 0.5f;
}
void Entity::cnew(EC_Logic *c)
{
	c_log.reset(c);
	c->ent = this;
}
void Entity::cnew(EC_Movement *c)
{
	c_mov.reset(c);
	c->ent = this;
}
void Entity::cnew(EC_Physics *c)
{
	c_phy.reset(c);
	c->ent = this;
}
void Entity::cnew(EC_Render *c)
{
	c_ren.reset(c);
	c->ent = this;
}
