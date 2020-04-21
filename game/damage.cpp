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
int HealthPool::apply(int amount, bool limited)
{
	if (limited) amount = std::min(amount, std::max(hp, hp_max) - hp);
	hp += amount;
	if (hp < 0) hp = 0;
	if (amount < 0) tmo = regen_wait;
	return amount;
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



EC_Health::EC_Health(Entity& ent, int hp)
	: EComp(ent), hp(hp)
{}
bool EC_Health::apply(DamageQuant q)
{
	DamageType orig_type = q.type;
	last_damaged = ent.core.get_step_time();
	
	if (q.amount < 0) hp.apply(-q.amount); // heal
	else
	{
		// armor
		
		if (q.armor && phys[*q.armor])
		{
			phys[*q.armor]->proc(*this, q);
			if (q.amount <= 0) goto zero_damage;
		}
		
		// filter
		
		for (auto& f : fils)
		{
			if (!f) continue;
			f->proc(*this, q);
			if (q.amount <= 0) goto zero_damage;
		}
		
		// apply damage
		
		if (q.wpos)
			GamePresenter::get()->effect(FE_HIT, {Transform{*q.wpos}, q.amount * 0.1f});
		
		hp.apply(-q.amount);
		if (!hp.is_alive())
		{
			ent.destroy();
			return false;
		}
zero_damage:
		
		DamageQuant ev = q;
		ev.type = orig_type;
		if (ev.amount < 0) ev.amount = 0;
		on_damage.signal(ev);
	}
	return true;
}
TimeSpan EC_Health::since_damaged() const
{
	return ent.core.get_step_time() - last_damaged;
}
void EC_Health::add_phys(std::unique_ptr<DamageFilter> f)
{
	size_t i=0;
	for (; i < phys.size(); ++i) if (!phys[i]) break;
	if (i == phys.size()) phys.emplace_back();
	phys[i] = std::move(f);
	
	upd_reg();
}
void EC_Health::add_filter(std::unique_ptr<DamageFilter> f)
{
	fils.insert(fils.begin(), std::move(f));
	upd_reg();
}
void EC_Health::rem_phys(DamageFilter* f) noexcept
{
	if (erase_if(phys, [&](auto& v){ return v.get() == f; }))
		upd_reg();
}
void EC_Health::rem_filter(DamageFilter* f) noexcept
{
	if (erase_if(fils, [&](auto& v){ return v.get() == f; }))
		upd_reg();
}
size_t EC_Health::get_phys_index(DamageFilter* f) const
{
	for (size_t i=0; i < phys.size(); ++i) if (phys[i].get() == f) return i;
	THROW_FMTSTR("EC_Health::get_phys_index() not found - {}", ent.dbg_id());
}
void EC_Health::foreach_filter(callable_ref<void(DamageFilter&)> f)
{
	for (auto& p : phys) if (p) f(*p);
	for (auto& p : fils) f(*p);
}
void EC_Health::upd_reg()
{
	bool hs = hp.has_regen();
	foreach_filter([&](auto& f){ hs |= f.has_step(); });
	
	if (hs) reg(ECompType::StepPostUtil);
	else  unreg(ECompType::StepPostUtil);
}
void EC_Health::step()
{
	foreach_filter([&](auto& f){ f.step(*this); });
	hp.step();
}



DmgShield::DmgShield(int capacity, int regen_per_second, TimeSpan regen_wait, std::optional<FixtureCreate> fc)
	: hp(capacity)
{
	hp.set_hps(regen_per_second);
	hp.regen_wait = regen_wait;
	
	if (fc) {
		if (!fc->info) fc->info = FixtureInfo{};
		fix.emplace(std::move(*fc));
	}
	enabled = is_phys() ? false : true;
}
void DmgShield::set_enabled(Entity& ent, bool on)
{
	if (enabled == on) return;
	enabled = on;
	
	if (is_phys())
	{
		if (enabled) fix->get_fc().info->armor_index = ent.ref_hlc().get_phys_index(this);
		fix->set_enabled(ent, enabled);
	}
}
void DmgShield::proc(EC_Health& hlc, DamageQuant& q)
{
	constexpr int dead_absorb = 100; ///< How much additional damage absorbed on destruction
	constexpr float absorb_per = 0.2; // Minimal HP for absorb to take effect (percentage)
	constexpr int   absorb_thr = 50;  // Minimal HP for absorb to take effect (value)
	
	if (!enabled || !hp.is_alive()) {
		hp.apply(-1); // reset regen timer
		return;
	}
	auto dmg_ren = q.amount;
	
	auto absorb = hp.t_state() > absorb_per || hp.exact().first > absorb_thr ? dead_absorb : 0;
	auto am_left = q.amount - hp.exact().first;
	hp.apply(-q.amount);
	q.amount = am_left - absorb;
	
	if (!is_phys()) {
		auto& phy = hlc.ent.ref_pc();
		
		ParticleBatchPars bp;
		int times = 1;
		
		if (hp.is_alive()) {
			if (hit_ren_tmo.is_positive()) return;
			hit_ren_tmo = TimeSpan::seconds(0.5);
			
			bp.power = dmg_ren * 0.02f;
			bp.clr = FColor(0.4, 1, 1, 0.5);
		}
		else {
			bp.power = 3;
			bp.clr = FColor(1, 0.5, 0.9, 2);
			times = 3;
		}
		
		bp.tr = phy.get_trans();
		bp.rad = phy.get_radius() + 0.5f;
		
		for (int i=0; i<times; ++i)
			GamePresenter::get()->effect(FE_CIRCLE_AURA, bp);
	}
	else if (q.wpos) {
		GamePresenter::get()->effect(FE_HIT_SHIELD, {Transform{*q.wpos}, hp.t_state() * 3.f});
		q.wpos.reset();
	}
}
void DmgShield::step(EC_Health& hlc)
{
	int hp_has = hp.exact().first;
	
	hp.step();
	if (hit_ren_tmo.is_positive()) hit_ren_tmo -= GameCore::step_len;
	
	if (!is_phys() && enabled)
	{
		int hp_now = hp.exact().first;
		if (hp_has == hp_now)
			return;
		
		ParticleBatchPars bp;
		bp.clr = FColor(0.3, 0.3, 1, 1);
		
		if		(hp_has <= 0) bp.power = 1;
		else if (hp_now == hp.exact().second) bp.power = 2;
		else {
			if (hit_ren_tmo.is_positive())
				return;
			bp.power = 0.3;
			bp.clr.a = 0.4;
		}
		
		auto& phy = hlc.ent.ref_pc();
		bp.tr = phy.get_trans();
		bp.rad = phy.get_radius() + 0.5f;
		GamePresenter::get()->effect(FE_CIRCLE_AURA, bp);
	}
}



DmgArmor::DmgArmor(int hp_max, int hp)
	: hp(hp, hp_max)
{
	k_self = 0.08;
	k_mod_min = 1; // 0% blocked
	k_mod_max = 0.3; // 70% blocked
	maxmod_t = 0.4;
}
void DmgArmor::proc(EC_Health&, DamageQuant& q)
{
	if (!hp.is_alive()) return;
	if (q.type != DamageType::Kinetic) return;
	
	int orig = q.amount;
	q.amount *= lerp(k_mod_min, k_mod_max, std::min(1.f, hp.t_state() / maxmod_t));
	
	/*if (q.amount < dmg_thr) q.amount = 0;
	else*/ hp.apply(-orig * k_self);
}
