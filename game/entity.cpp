#include "entity.hpp"
#include "game_core.hpp"
#include "logic.hpp"
#include "physics.hpp"
#include "presenter.hpp"



Entity::Entity( GameCore& core, EntityIndex index ) : index( index ), core( core ) {}
Entity::~Entity() = default;
void Entity::destroy() {core.mark_deleted(this);}



Transform Entity::get_pos() const
{
	if (!c_phy) return {};
	return conv(c_phy->body->GetTransform());
}
float Entity::get_dir() const
{
	if (!c_phy) return 0.f;
	return c_phy->body->GetAngle();
}
void Entity::cnew(EC_Logic   *c)
{
	c_log.reset(c);
	c->ent = this;
}
void Entity::cnew(EC_Physics *c)
{
	c_phy.reset(c);
	c->ent = this;
}
void Entity::cnew(EC_Render  *c)
{
	c_ren.reset(c);
	c->ent = this;
}
