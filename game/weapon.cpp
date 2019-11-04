#include "client/presenter.hpp"
#include "utils/noise.hpp"
#include "game_core.hpp"
#include "physics.hpp"
#include "weapon.hpp"



ModelType ammo_model(AmmoType type)
{
	switch (type)
	{
	case AmmoType::Bullet: return MODEL_MINIGUN_AMMO;
	case AmmoType::Rocket: return MODEL_ROCKET_AMMO;
	case AmmoType::Energy: return MODEL_ELECTRO_AMMO;
		
	case AmmoType::None:
	case AmmoType::TOTAL_COUNT:
		break;
	}
	return MODEL_ERROR;
}



bool Weapon::is_ready()
{
	return equip->has_ammo(*this);
}
void Weapon::Overheat::shoot(float amount)
{
	value += amount;
	if (value >= 1) flag = true;
}
void Weapon::Overheat::cool()
{
	value -= (flag? v_cool : v_decr) * GameCore::time_mul;
	if (value < 0) value = 0;
	if (value < thr_off) flag = false;
}
float EC_Equipment::Ammo::add(float amount)
{
	amount = clampf(amount, -value, max - value);
	value += amount;
	return amount;
}



EC_Equipment::EC_Equipment(Entity* ent)
	 : EComp(ent)
{
	reg(ECompType::StepPostUtil);
	
	ammos[static_cast<size_t>(AmmoType::Bullet)].max = 450;
	ammos[static_cast<size_t>(AmmoType::Rocket)].max = 40;
	ammos[static_cast<size_t>(AmmoType::Energy)].max = 60;
}
void EC_Equipment::try_shoot(vec2fp target, bool main, bool alt)
{
	if (!main && !prev_main && !alt && !prev_alt) return;
	Weapon::ShootParams pars = {target, main, prev_main, alt, prev_alt};
	prev_main = main;
	prev_alt = alt;
	shoot(pars);
}
bool EC_Equipment::shoot(Weapon::ShootParams pars)
{
	if (has_shot) return true;
	
	auto wpn = wpn_ptr();
	if (!wpn || !wpn->is_ready()) return false;
	
	if (wpn->rof_left.is_positive()) return false;
	if (!has_ammo(*wpn)) return false;
	if (wpn->overheat && !wpn->overheat->is_ok()) return false;
	
	has_shot = true;
	auto res = wpn->shoot(pars);
	if (!res) return false;
	
	if (!res->ammo)  res->ammo  = wpn->info->def_ammo;
	if (!res->delay) res->delay = wpn->info->def_delay;
	if (!res->heat)  res->heat  = wpn->info->def_heat;
	
	if (!res->delay || *res->delay < GameCore::step_len)
		res->delay = GameCore::step_len;
	
	if (wpn->overheat) wpn->overheat->shoot(*res->heat * res->delay->seconds());
	wpn->rof_left = *res->delay;
	if (!infinite_ammo) get_ammo(wpn->info->ammo).add(-*res->ammo);
	
	return true;
}
bool EC_Equipment::set_wpn(std::optional<size_t> index)
{
	if (index == wpn_cur) return true;
	if (index >= wpns.size()) return false;
	
	// check if can be holstered
	if (auto wpn = wpn_ptr())
	{
		bool ok = true;
		if (wpn->overheat && !wpn->overheat->is_ok()) ok = false;
		
		if (!ok)
		{
			last_req = index;
			return false;
		}
	}
	
	// check if can be equipped
	if (index)
	{
		auto& wpn = wpns[*index];
		if (!infinite_ammo && !get_ammo(wpn->info->ammo).has(*wpn))
		{
			last_req.reset();
			return false;
		}
	}
	
	// reset
	if (auto rc = ent->get_ren())
		rc->detach(ECompRender::ATT_WEAPON);
	
	wpn_cur = index;
	
	last_req.reset();
	has_shot = false;
	
	// update
	if (auto wpn = wpn_ptr())
	{
		auto rc = ent->get_ren();
		if (rc) {
			auto& ri = *wpn->info;
			int w_hand = ri.hand ? *ri.hand : hand;
			
			float r = ent->get_phy().get_radius();
			rc->attach( ECompRender::ATT_WEAPON, Transform{vec2fp(r, r/2 * w_hand)}, ri.model, FColor(1, 0.4, 0, 1) );
		}
	}
	return true;
}
Weapon* EC_Equipment::wpn_ptr()
{
	return wpn_cur ? wpns[*wpn_cur].get() : nullptr;
}
Weapon& EC_Equipment::get_wpn()
{
	if (!wpn_cur) throw std::runtime_error("EC_Equipment::get_wpn() null");
	return *wpns[*wpn_cur];
}
void EC_Equipment::add_wpn(Weapon* wpn)
{
	wpns.emplace_back(wpn);
	wpn->equip = this;
}
bool EC_Equipment::has_ammo(Weapon& w, std::optional<float> amount)
{
	if (infinite_ammo) return true;
	if (!amount) amount = w.info->def_ammo;
	return amount ? get_ammo(w.info->ammo).has(*amount) : true;
}
void EC_Equipment::step()
{
	if (last_req)
		set_wpn(*last_req);
	
	for (auto& wpn : wpns)
	{
		if (wpn->rof_left.is_positive())
			wpn->rof_left -= GameCore::step_len;
		
		if (wpn->overheat && (wpn.get() != wpn_ptr() || !has_shot))
			wpn->overheat->cool();
	}
	
	has_shot = false;
}
