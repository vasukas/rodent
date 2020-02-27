#include "vaslib/vas_log.hpp"
#include "game_core.hpp"
#include "physics.hpp"



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
	if (_reg)
		ent.core.unreg_c( _reg->type, _reg->index );
}
void EComp::reg(ECompType type)
{
	if (_reg)
	{
		if (_reg->type == type) return;
		THROW_FMTSTR("EComp::reg() already - {}", ent.dbg_id());
	}
	_reg = ComponentRegistration{ type, ent.core.reg_c(type, this) };
}
void EComp::unreg(ECompType type)
{
	if (_reg && _reg->type == type)
	{
		ent.core.unreg_c( _reg->type, _reg->index );
		_reg.reset();
	}
}



AI_Drone& Entity::ref_ai_drone()
{
	if (auto p = get_ai_drone()) return *p;
	THROW_FMTSTR("Entity::ref_ai_drone() failed - {}", dbg_id());
}
EC_Equipment& Entity::ref_eqp()
{
	if (auto p = get_eqp()) return *p;
	THROW_FMTSTR("Entity::ref_eqp() failed - {}", dbg_id());
}
EC_Health& Entity::ref_hlc()
{
	if (auto p = get_hlc()) return *p;
	THROW_FMTSTR("Entity::ref_hlc() failed - {}", dbg_id());
}
EC_Physics& Entity::ref_phobj()
{
	if (auto p = dynamic_cast<EC_Physics*>(&ref_pc())) return *p;
	THROW_FMTSTR("Entity::ref_phobj() failed - {}", dbg_id());
}



std::string Entity::dbg_id() const
{
	return FMT_FORMAT("eid {}, descr \"{}\"", index.to_int(), ui_descr);
}
void Entity::destroy()
{
	if (was_destroyed) return;
	core.on_ent_destroy(this);
	was_destroyed = true;
}
void Entity::reg_this() noexcept
{
	if (!reglist_index)
		reglist_index = core.reg_ent(this);
}
void Entity::unreg_this() noexcept
{
	if (reglist_index) {
		core.unreg_ent(*reglist_index);
		reglist_index.reset();
	}
}
Entity::Entity(GameCore& core)
    : core(core), index(core.on_ent_create(this))
{}
Entity::~Entity()
{
	iterate_reverse([&](auto& c){ delete c; c = {}; });
	unreg_this();
}



void Entity::throw_type_error(const char *func, const std::type_info& t) const
{
	THROW_FMTSTR("Entity::{} for type '{}' failed", func, human_readable(t));
}
void Entity::iterate_direct (callable_ref<void(EComp*&)> f)
{
	for (int i = 0; i != n_dyn_comps(); ++i)
		for (auto& c : dyn_comps)
			if (c.second.order == i) {
				f(c.second.c);
				break;
			}
}
void Entity::iterate_reverse(callable_ref<void(EComp*&)> f)
{
	for (int i = n_dyn_comps() - 1; i != -1; --i)
		for (auto& c : dyn_comps)
			if (c.second.order == i) {
				f(c.second.c);
				break;
			}
}
