#include "client/presenter.hpp"
#include "vaslib/vas_log.hpp"
#include "damage.hpp"
#include "game_core.hpp"
#include "physics.hpp"



HealthPool::HealthPool(int hp, std::optional<int> hp_max)
	: hp(hp), hp_max(hp_max? *hp_max : hp)
{}
float HealthPool::t_state() const
{
	return clampf_n( static_cast<float>(hp) / hp_max );
}
void HealthPool::apply(int amount, bool limited)
{
	hp += amount;
	if (limited && hp > hp_max) hp = hp_max;
	if (amount < 0) tmo = regen_wait;
}
void HealthPool::renew(std::optional<int> new_max)
{
	if (new_max) hp_max = *new_max;
	apply(hp_max);
	tmo = {};
}
void HealthPool::set_hps(int hps)
{
	regen_hp = hps / 2;
	regen_cd.set_seconds(0.5);
}
void HealthPool::step()
{
	if (tmo.is_positive()) tmo -= GameCore::step_len;
	else if (t_state() < regen_at)
	{
		apply(regen_hp);
		tmo = regen_cd;
	}
}



EC_Health::EC_Health(Entity* ent, int hp)
	: EComp(ent), hp(hp)
{}
void EC_Health::apply(DamageQuant q)
{
	DamageType orig_type = q.type;
	
	// convert
	
	if (q.type == DamageType::Physical)
		q.amount = (q.amount - ph_thr) * ph_k;
	
	if (q.amount <= 0) return;
	
	// armor
	
	if (q.armor && pr_area[*q.armor])
	{
		pr_area[*q.armor]->proc(*this, q);
		if (q.amount <= 0) return;
	}
	
	// filter
	
	for (auto& f : fils)
	{
		if (!f) continue;
		f->proc(*this, q);
		if (q.amount <= 0) return;
	}
	
	// apply damage
	
	if (q.wpos)
		GamePresenter::get()->effect(FE_HIT, {Transform{*q.wpos}, q.amount * 0.1f});
	
	hp.apply(-q.amount);
	if (!hp.is_alive())
	{
		ent->destroy();
		return;
	}
	
	DamageQuant ev = q;
	ev.type = orig_type;
	on_damage.signal(ev);
}
void EC_Health::upd_hp()
{
	bool hs = hp.has_regen();
	if (!hs) {
		for (auto& f : pr_area)
			if (f && f->has_step())
				{hs = true; break;}
	}
	
	if (hs) reg(ECompType::StepPostUtil);
	else  unreg(ECompType::StepPostUtil);
}
void EC_Health::hook(EC_Physics& ph)
{
	EVS_CONNECT1(ph.ev_contact, on_event);
}
void EC_Health::on_event(const CollisionEvent& ev)
{
	if (ev.type == CollisionEvent::T_RESOLVE)
		apply({ DamageType::Physical, static_cast<int>(ev.imp) });
}
size_t EC_Health::add_filter(std::shared_ptr<DamageFilter> f, std::optional<size_t> index)
{
	return add(fils, std::move(f), index);
}
void EC_Health::rem_filter(size_t i)
{
	rem(fils, i);
}
size_t EC_Health::add_prot(std::shared_ptr<DamageFilter> f, std::optional<size_t> index)
{
	return add(pr_area, std::move(f), index);
}
void EC_Health::rem_prot(size_t i)
{
	rem(pr_area, i);
}
size_t EC_Health::add(std::vector<std::shared_ptr<DamageFilter>>& fs, std::shared_ptr<DamageFilter> f, std::optional<size_t> index)
{
	size_t i = index? *index : fs.size();
	bool hs = f->has_step();
	
	if (i >= fs.size()) fs.resize( i + 1 );
	if (fs[i]) GAME_THROW("EC_Health::add() index {} already taken (ent {})", i, ent->dbg_id());
	fs[i] = std::move(f);
	
	if (hs && !hp.has_regen()) reg(ECompType::StepPostUtil);
	return i;
}
void EC_Health::rem(std::vector<std::shared_ptr<DamageFilter>>& fs, size_t i)
{
	if (i >= fs.size() || !fs[i]) return;
	
	bool hs = fs[i]->has_step();
	fs.erase( fs.begin() + i );
	
	if (hs && !hp.has_regen()) upd_hp();
}
void EC_Health::step()
{
	for (auto& f : pr_area) if (f) f->step(*this);
	hp.step();
}



DmgShield::DmgShield(int capacity, int regen_per_second, TimeSpan regen_wait)
	: hp(capacity)
{
	hp.set_hps(regen_per_second);
	hp.regen_wait = regen_wait;
}
void DmgShield::proc(EC_Health&, DamageQuant& q)
{
	if (!enabled || !hp.is_alive()) return;
	hp.apply(-q.amount);
	q.amount = 0;
	
	if (q.wpos) {
		GamePresenter::get()->effect(FE_HIT_SHIELD, {Transform{*q.wpos}, hp.t_state() * 3.f});
		q.wpos.reset();
	}
}
void DmgShield::step(EC_Health&)
{
	hp.step();
}



DmgArmor::DmgArmor(int hp_max, int hp)
	: hp(hp, hp_max)
{}
void DmgArmor::proc(EC_Health&, DamageQuant& q)
{
	if (!hp.is_alive()) return;
	if (q.type != DamageType::Kinetic) return;
	
	if (q.amount < thr) q.amount = 0;
	else {
		q.amount *= mod;
		hp.apply(-q.amount * self);
	}
}
