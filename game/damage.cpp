#include "damage.hpp"
#include "physics.hpp"

void EC_Health::damage(DamageQuant q, std::optional<DamageType> calc_type)
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
		damage(q, DamageType::Direct);
	}
	break;
	case DamageType::Kinetic:
	{
		damage(q, DamageType::Direct);
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
		damage({ DamageType::Physical, ev.imp });
}
