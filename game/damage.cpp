#include "damage.hpp"
#include "physics.hpp"

void EC_Health::damage(DamageType type, float val)
{
	if (invincible) return;
	if (type == DamageType::Direct)
	{
		hp -= val;
		if (hp < 0) {
			ent->destroy();
			return;
		}
		
		DamageEvent ev;
		ev.amount = val;
		on_damage.signal(ev);
	}
	else if (type == DamageType::Physical)
	{
		if (val < ph_thr) return;
		val *= ph_k;
		damage(DamageType::Direct, val);
	}
}
void EC_Health::hook(EC_Physics& ph)
{
	EVS_CONNECT1(ph.ev_contact, on_event);
}
void EC_Health::on_event(const ContactEvent& ev)
{
	if (ev.type == ContactEvent::T_RESOLVE)
		damage(DamageType::Physical, ev.imp);
}
