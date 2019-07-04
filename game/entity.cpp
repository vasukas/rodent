#include "vaslib/vas_log.hpp"
#include "game_core.hpp"
#include "physics.hpp"



EComp::~EComp()
{
	for (auto& r : _regs)
		GameCore::get().unreg_c( r.type, r.index );
}
void EComp::reg(ECompType type) noexcept
{
	for (auto& r : _regs) if (r.type == type) return;
	_regs.push_back({ type, GameCore::get().reg_c(type, this) });
}
void EComp::unreg(ECompType type) noexcept
{
	auto it = std::find_if( _regs.begin(), _regs.end(), [type](auto& v){return v.type == type;} );
	if (it != _regs.end()) {
		GameCore::get().unreg_c( it->type, it->index );
		_regs.erase(it);
	}
}



bool Entity::is_ok() const {
	return !was_destroyed;
}
void Entity::destroy() {
	core.mark_deleted(this);
	was_destroyed = true;
}
Entity::Entity( GameCore& core, EntityIndex index ) : index( index ), core( core ) {}
Entity::~Entity() {
	for (auto it = cs_ord.rbegin(); it != cs_ord.rend(); ++it) it->reset();
}



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
	p.rotate( get_pos().rot );
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
