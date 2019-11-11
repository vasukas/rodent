#include "vaslib/vas_log.hpp"
#include "game_core.hpp"
#include "physics.hpp"

void EntityDeleter::operator()(Entity* p) {p->destroy();}



const char *enum_name(ECompType type)
{
	switch (type)
	{
	case ECompType::StepPreUtil:  return "PreUtil";
	case ECompType::StepLogic:    return "Logic";
	case ECompType::StepPostUtil: return "PostUtil";
	case ECompType::TOTAL_COUNT:  return "TOTAL_COUNT"; 
	}
	return "INVALID";
}



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



vec2fp ECompPhysics::get_norm_dir() const
{
	vec2fp p{1, 0};
	p.rotate( get_trans().rot );
	return p;
}



EC_Physics& Entity::get_phobj()
{
	return dynamic_cast<EC_Physics&>(get_phy());
}
ECompPhysics& Entity::get_phy()
{
	THROW_FMTSTR("Entity::get_phy() null ({})", dbg_id());
}
std::string Entity::dbg_id() const
{
	return FMT_FORMAT("eid {}, descr \"{}\"", index.to_int(), ui_descr());
}
bool Entity::is_ok() const
{
	return !was_destroyed;
}
void Entity::destroy()
{
	if (was_destroyed) return;
	was_destroyed = true;
	GameCore::get().mark_deleted(this);
}
Entity::Entity()
    : index(GameCore::get().create_ent(this))
{}
Entity::~Entity()
{
	unreg();
}
void Entity::reg() noexcept
{
	if (!reglist_index)
		reglist_index = GameCore::get().reg_ent(this);
}
void Entity::unreg() noexcept
{
	if (reglist_index) {
		GameCore::get().unreg_ent(*reglist_index);
		reglist_index.reset();
	}
}
