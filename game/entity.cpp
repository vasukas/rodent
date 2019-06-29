#include "vaslib/vas_log.hpp"
#include "game_core.hpp"
#include "physics.hpp"



EComp::~EComp()
{
	if (reg_step_id != size_t_inval)
		ent->core.unreg_step(reg_step_id);
}
void EComp::reg_step(GameStepOrder order)
{
	if (reg_step_id == size_t_inval)
		reg_step_id = GameCore::get().reg_step(*this, order);
}



Entity::Entity( GameCore& core, EntityIndex index ) : index( index ), core( core ) {}
Entity::~Entity() {cs_ord.clear();}
void Entity::destroy() {core.mark_deleted(this);}



Transform Entity::get_pos() const
{
	if (auto c = get<EC_Physics>())
		return conv(c->body->GetTransform());
	return {};
}
Transform Entity::get_vel() const
{
	if (auto c = get<EC_Physics>())
		return {conv(c->body->GetLinearVelocity()), c->body->GetAngularVelocity()};
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
	if (auto c = get<EC_Physics>())
		return c->b_radius;
	return 0.5f;
}
void Entity::add(std::type_index type, EComp* c)
{
	if (!c) return;
	if (get(type)) GAME_THROW("Entity::add() already exists: {} in {} ({})", c->get_typename(), index, dbg_name);
	
	c->ent = this;
	cs.emplace(type, c);
	cs_ord.emplace_back(c);
}
void Entity::rem(std::type_index type) noexcept
{
	auto it = cs.find(type);
	if (it == cs.end()) return;
	
	auto pi = std::find_if( cs_ord.begin(), cs_ord.end(), [&it](auto&& v){return v.get() == it->second;} );
	cs_ord.erase(pi);
	cs.erase(it);
}
EComp* Entity::get(std::type_index type) const noexcept
{
	auto it = cs.find(type);
	return it != cs.end() ? it->second : nullptr;
}
EComp& Entity::getref(std::type_index type) const
{
	EComp* p = get(type);
	if (!p) GAME_THROW("Entity::getref() not exists: in {} ({})", index, dbg_name);
	return *p;
}
