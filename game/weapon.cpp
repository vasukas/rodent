#include "client/ec_render.hpp"
#include "game_core.hpp"
#include "physics.hpp"
#include "weapon.hpp"



ModelType ammo_model(AmmoType type)
{
	switch (type)
	{
	case AmmoType::Bullet:   return MODEL_MINIGUN_AMMO;
	case AmmoType::Rocket:   return MODEL_ROCKET_AMMO;
	case AmmoType::Energy:   return MODEL_ELECTRO_AMMO;
	case AmmoType::FoamCell: return MODEL_GRENADE_AMMO;
		
	case AmmoType::None:
	case AmmoType::TOTAL_COUNT:
		break;
	}
	return MODEL_ERROR;
}
const char *ammo_name(AmmoType type)
{
	switch (type)
	{
	case AmmoType::Bullet:   return "Bullets";
	case AmmoType::Rocket:   return "Rockets";
	case AmmoType::Energy:   return "Battery";
	case AmmoType::FoamCell: return "Foam fuel";
		
	case AmmoType::None:     return "Ether";
	case AmmoType::TOTAL_COUNT:
		break;
	}
	return "ERROR";
}
void Weapon::Info::set_origin_from_model()
{
	bullet_offset = ResBase::get().get_cpt(model);
}



std::optional<Weapon::DirectionResult> Weapon::get_direction(const ShootParams& pars, DirectionType dtype)
{
	auto& ent = equip->ent;
	
	vec2fp orig = ent.ref_pc().get_pos();
	float rot = ent.ref_pc().get_angle();
	
	bool forward_dir;
	if (dtype == DIRTYPE_IGNORE_ANGLE) forward_dir = true;
	else
	{
		float ta = (pars.target - orig).fastangle();
		if (std::fabs(wrap_angle( ta - rot )) > info->angle_limit)
		{
			if (dtype == DIRTYPE_TARGET) return {};
			forward_dir = true;
		}
		else forward_dir = false;
	}
	
	vec2fp offset = equip->get_attachment_offset();
	offset.fastrotate(rot);
	if (equip->hand)
		offset += vec2fp(info->bullet_offset).fastrotate( rot );
	
	if (auto rc = ent.core.get_phy().raycast_nearest( conv(orig), conv(orig + offset) )) {
		offset = 0.9 * (conv(rc->poi) - orig);
	}
	orig += offset;
	
	vec2fp dir = forward_dir ? offset : pars.target - orig;
	dir.norm();

	return DirectionResult{orig, dir};
}
void Weapon::sound(SoundId id, std::optional<TimeSpan> period, std::optional<float> t)
{
	auto& ent = equip->ent;
	if (period) {
		equip->snd_loop.update({ id, ent.get_pos(), ent.index, t, *period });
		equip->snd_keep_until = equip->ent.core.get_step_time() + *period + GameCore::step_len;
	}
	else {
		equip->snd_loop.stop();
		SoundEngine::once(id, ent.get_pos());
	}
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
float EC_Equipment::Ammo::add(int amount)
{
	amount = clamp(amount, -value, max - value);
	value += amount;
	return amount;
}



EC_Equipment::EC_Equipment(Entity& ent)
	 : EComp(ent)
{
	reg(ECompType::StepPostUtil);
	
	ammos[static_cast<size_t>(AmmoType::Bullet)].max = 450;
	ammos[static_cast<size_t>(AmmoType::Rocket)].max = 40;
	ammos[static_cast<size_t>(AmmoType::Energy)].max = 70;
	ammos[static_cast<size_t>(AmmoType::FoamCell)].max = 100;
}
void EC_Equipment::shoot(vec2fp target, bool main, bool alt)
{
	pars.target = target;
	pars.main = main;
	pars.alt = alt;
}
bool EC_Equipment::set_wpn(size_t index, bool even_if_no_ammo)
{
	if (index == wpn_cur) return true;
	if (index >= wpns.size()) return false;
	
	// check if can be equipped
	if (!even_if_no_ammo)
	{
		auto& wpn = wpns[index];
		if (!infinite_ammo && !get_ammo(wpn->info->ammo).has(*wpn))
		{
			if (msgrep) msgrep->jerr(WeaponMsgReport::ERR_SELECT_NOAMMO);
			return false;
		}
	}
	
	// check if current can be holstered
	if (wpn_cur != size_t_inval)
	{
		auto& wpn = get_wpn();
		if (wpn.overheat && !wpn.overheat->is_ok())
		{
//			last_req = index;
			if (msgrep) msgrep->jerr(WeaponMsgReport::ERR_SELECT_OVERHEAT);
			return false;
		}
	}
	
	// reset
	auto& rc = ent.ensure<EC_RenderEquip>();
	rc.detach(EC_RenderEquip::ATT_WEAPON);
	
	if (wpn_cur != size_t_inval) w_prev = wpn_cur;
	wpn_cur = index;
	last_req.reset();
	
	// update
	if (hand)
	{
		auto& ri = *get_wpn().info;
		rc.attach( EC_RenderEquip::ATT_WEAPON, Transform{get_attachment_offset()}, ri.model, FColor(1, 0.4, 0, 1) );
	}
	
	return true;
}
size_t EC_Equipment::wpn_index()
{
	if (wpns.empty()) throw std::runtime_error("EC_Equipment::wpn_index() no weapons");
	return wpn_cur;
}
Weapon& EC_Equipment::get_wpn()
{
	if (wpns.empty()) throw std::runtime_error("EC_Equipment::get_wpn() no weapons");
	return *wpns[wpn_cur];
}
void EC_Equipment::add_wpn(std::unique_ptr<Weapon> wpn)
{
	wpn->equip = this;
	wpns.emplace_back(std::move(wpn));
	
	if (wpns.size() == 1) {
		wpn_cur = size_t_inval;
		set_wpn(0);
		wpn_cur = 0;
	}
}
void EC_Equipment::replace_wpn(size_t index, std::unique_ptr<Weapon> new_wpn)
{
	auto& wpn = wpns.at(index);
	wpn = std::move(new_wpn);
	wpn->equip = this;
}
bool EC_Equipment::has_ammo(Weapon& w, std::optional<int> amount)
{
	if (infinite_ammo) return true;
	if (!amount) amount = w.info->def_ammo;
	return amount ? get_ammo(w.info->ammo).has(*amount) : true;
}
vec2fp EC_Equipment::get_attachment_offset()
{
	vec2fp ret;
	if (auto p = ResBase::get().get_cpt_opt(ent.ref<EC_RenderModel>().model)) ret = *p;
	else ret = {ent.ref_pc().get_radius(), 0};
	
	// No hands are used
//	auto& ri = *get_wpn().info;
//	int w_hand = ri.hand ? *ri.hand : *hand;
//	float r = ent.ref_pc().get_radius();
//	ret.y += r/2 * w_hand;
	
	return ret;
}
bool EC_Equipment::shoot_internal(Weapon& wpn, Weapon::ShootParams pars)
{
	auto res = wpn.shoot(pars);
	if (!res) return false;
	
	if (!res->ammo)  res->ammo  = wpn.info->def_ammo;
	if (!res->delay) res->delay = wpn.info->def_delay;
	if (!res->heat)  res->heat  = wpn.info->def_heat;
	
	if (!res->delay || *res->delay < GameCore::step_len)
		res->delay = GameCore::step_len;
	
	if (wpn.overheat && res->heat) wpn.overheat->shoot(*res->heat * res->delay->seconds());
	wpn.rof_left = *res->delay;
	if (!infinite_ammo && res->ammo) get_ammo(wpn.info->ammo).add(-*res->ammo);
	
	return true;
}
int EC_Equipment::shoot_check(Weapon& wpn)
{
	if (wpn.rof_left.is_positive()) return 0;
	if (!no_overheat && wpn.overheat && !wpn.overheat->is_ok()) return 0;
	if (!has_ammo(wpn)) return 1;
	return 2;
}
void EC_Equipment::step()
{
	did_shot_flag = false;
	if (wpns.empty()) return;
	
	if (last_req)
		set_wpn(*last_req);
	
	bool snd_shooting = pars.main || pars.alt;
	if (w_prev && *w_prev != wpn_cur)
	{
		auto sp = pars;
		sp.main = sp.alt = false;
		shoot_internal(*wpns[*w_prev], sp);
		w_prev.reset();
	}
	
	int shcheck = shoot_check(get_wpn());
	if (shcheck != 2) pars.main = pars.alt = false;
	pars.is_ok = (shcheck != 0);
	
	bool has_shot = (pars.main || pars.alt || pars.main_was || pars.alt_was) && shoot_internal(get_wpn(), pars);
	pars.main_was = pars.main;
	pars.alt_was = pars.alt;
	pars.main = pars.alt = false;
	did_shot_flag = has_shot;
	
	for (size_t i=0; i < wpns.size(); ++i)
	{
		auto& wpn = wpns[i];
		
		if (wpn->rof_left.is_positive())
			wpn->rof_left -= GameCore::step_len;
		
		if (wpn->overheat && (i != wpn_cur || !has_shot))
			wpn->overheat->cool();
	}
	
	if (!snd_shooting || (!has_shot && !get_wpn().rof_left.is_positive()
	                      && !get_wpn().is_preparing() && ent.core.get_step_time() > snd_keep_until))
		snd_loop.stop();
}
