#include "damage.hpp"
#include "physics.hpp"

EC_Health::EC_Health(Entity* ent, int hp)
    : EComp(ent), hp(hp), hp_max(hp)
{}
bool EC_Health::is_alive() const
{
	return hp > 0;
}
float EC_Health::get_t_state() const
{
	return std::min( 1.f, static_cast<float>(hp) / hp_max );
}
void EC_Health::add_hp(int amount)
{
	hp = std::max(hp + amount, hp_max);
}
void EC_Health::renew_hp(std::optional<int> new_max)
{
	if (new_max) hp_max = *new_max;
	add_hp(hp_max);
}
void EC_Health::apply(DamageQuant q, std::optional<DamageType> calc_type)
{
	if (invincible) return;
	auto type = calc_type? *calc_type : q.type;
	
	switch (type)
	{
	case DamageType::Direct:
	{
		hp -= q.amount;
		if (hp < 0) {
			ent->destroy();
			return;
		}
		
		DamageQuant ev = q;
		ev.type = type;
		on_damage.signal(ev);
	}
	break;
	case DamageType::Physical:
	{
		q.amount -= ph_thr;
		if (q.amount < 0) return;
		q.amount *= ph_k;
		apply(q, DamageType::Direct);
	}
	break;
	case DamageType::Kinetic:
	{
		apply(q, DamageType::Direct);
	}
	break;
	}
}
void EC_Health::hook(EC_Physics& ph)
{
	EVS_CONNECT1(ph.ev_contact, on_event);
}
void EC_Health::on_event(const ContactEvent& ev)
{
	if (ev.type == ContactEvent::T_RESOLVE)
		apply({ DamageType::Physical, static_cast<int>(ev.imp) });
}
