#include "client/player_ui.hpp"
#include "client/plr_input.hpp"
#include "game/game_core.hpp"
#include "game/level_ctr.hpp"
#include "game_objects/objs_basic.hpp"
#include "game_objects/objs_player.hpp"
#include "utils/serializer_defs.hpp"
#include "vaslib/vas_log.hpp"
#include "player_mgr.hpp"


SERIALFUNC_PLACEMENT_1(PlayerNetworkHUD::WpnInfo,
	SER_FD(icon),
	SER_FD(ammo),
	SER_FD(ammomax));

SERIALFUNC_PLACEMENT_1(PlayerNetworkHUD,
	SER_FD(shlc_state),
	SER_FD(shlc_time),
	SER_FD(shlc_hp),
	SER_FD(shlc_hpmax),
	
	SER_FD(hlc_hp),
	SER_FD(hlc_hpmax),
	
	SER_FD(t_accel_enabled),
	SER_FDT(t_accel, norm8),
	
	SER_FD(arm_hp),
	SER_FD(arm_hpmax),
	SER_FD(arm_atmax),
	
	SER_FD(pers_hp),
	SER_FD(pers_hpmax),
	SER_FD(pers_shld_alive),
	
	SER_FDT(wpns, Array32),
	SER_FD(wpncur),
	
	SER_FD(wpn_reload_show),
	SER_FDT(wpn_reload, norm8),
	
	SER_FDT(wpn_overheat, norm8),
	SER_FD(wpn_overheat_ok),
	
	SER_FDT(wpn_charge, norm8)
);

void PlayerNetworkHUD::write(Entity& p_ent, File& f)
{
	PlayerEntity& ent = static_cast<PlayerEntity&>(p_ent);
	
	shlc_state = ent.log.shlc.get_state().first;
	shlc_time = ent.log.shlc.get_state().second;
	shlc_hp = ent.log.shlc.get_ft()->get_hp().exact().first;
	shlc_hpmax = ent.log.shlc.get_ft()->get_hp().exact().second;
	
	hlc_hp = ent.hlc.get_hp().exact().first;
	hlc_hpmax = ent.hlc.get_hp().exact().second;
	
	t_accel_enabled = ent.log.pmov.get_t_accel().first;
	t_accel = ent.log.pmov.is_infinite_accel() ? -1 : ent.log.pmov.get_t_accel().second;
	
	arm_hp = ent.log.armor->get_hp().exact().first;
	arm_hpmax = ent.log.armor->get_hp().exact().second;
	arm_atmax = ent.log.armor->get_hp().t_state() >= ent.log.armor->maxmod_t;
	
	pers_hp = ent.log.pers_shld->get_hp().exact().first;
	pers_hpmax = ent.log.pers_shld->get_hp().exact().second;
	pers_shld_alive = ent.log.pers_shld->get_hp().is_alive();
	
	auto& raw = ent.eqp.raw_wpns();
	wpns.reserve(raw.size());
	wpns.clear();
	for (auto& src : raw) {
		auto& wpn = wpns.emplace_back();
		wpn.icon = src->info->model;
		wpn.ammo = ent.eqp.get_ammo(src->info->ammo).value;
		wpn.ammomax = ent.eqp.get_ammo(src->info->ammo).max;
	}
	
	auto& cur = ent.eqp.get_wpn();
	wpncur = ent.eqp.wpn_index();
	
	if (auto m = cur.info->def_delay; m && *m > TimeSpan::seconds(0.5)) {
		wpn_reload_show = true;
		wpn_reload = cur.get_reload_timeout().value_or(TimeSpan::seconds(-1)) / *m;
	}
	else {
		wpn_reload_show = false;
	}
	
	if (auto& m = cur.overheat; m && m->value > 0) {
		wpn_overheat = m->value;
		wpn_overheat_ok = m->is_ok();
	}
	else {
		wpn_overheat = -1;
	}
	
	if (auto m = cur.get_ui_info(); m && m->charge_t) {
		wpn_charge = *m->charge_t;
	}
	else {
		wpn_charge = -1;
	}
	
	SERIALFUNC_WRITE(*this, f);
}
void PlayerNetworkHUD::read(File& f)
{
	SERIALFUNC_READ(*this, f);
	
	shlc_t = clampf_n(float(shlc_hp) / shlc_hpmax);
	hlc_t = clampf_n(float(hlc_hp) / hlc_hpmax);
	arm_t = clampf_n(float(arm_hp) / arm_hpmax);
	pers_t = clampf_n(float(pers_hp) / pers_hpmax);
}



class PlayerManager_Impl : public PlayerManager
{
public:
	const TimeSpan resp_time = TimeSpan::seconds(5);
	
	// control
	
	GameCore& core;
	
	TimeSpan plr_resp; // respawn timeout
	vec2fp last_plr_pos = {}; // for AI rects
	
	// entity
	
	EntityIndex plr_eid = {};
	PlayerEntity* plr_ent = nullptr;
	DmgIDDQD* invinc = nullptr;
	
	// interface
	
	TimeSpan interact_after;
	
	std::unique_ptr<PlayerUI> pui;
	std::string pui_lookat;
	EntityIndex pui_einter;
	
	
	
	PlayerManager_Impl(GameCore& core)
		: core(core)
	{}
	Entity* get_ent() override
	{
		return core.get_ent(plr_eid);
	}
	Entity& ref_ent() override
	{
		if (auto e = get_ent()) return *e;
		THROW_FMTSTR("PlayerManager::ref_ent() failed");
	}
	bool is_player(Entity& ent) const override
	{
		for (auto& e : nethack_server) {
			if (e.first == ent.index) {
				return true;
			}
		}
		return ent.index == plr_eid;
	}
	PlayerUI* get_pui() override
	{
		return pui.get();
	}
	
	
	
	void render(TimeSpan passed, vec2i cursor_pos) override
	{
		if (!pui) return;
		if (nethack_client) {
			pui->render(nethack_client->second, !!nethack_client->first, passed, cursor_pos);
			return;
		}
		
		PlayerUI::DrawState uis;
		uis.resp_left = plr_resp;
		uis.lookat = pui_lookat;
		uis.einter = dynamic_cast<EInteractive*>(core.valid_ent(pui_einter));
		
		pui->render(*this, uis, passed, cursor_pos);
	}
	void update_cheats() override
	{
		if (is_superman)
			cheat_godmode = true;

		if (cheat_godmode)
		{
			if (!plr_ent) plr_resp = {};
			else {
				invinc = new DmgIDDQD;
				plr_ent->ref_hlc().add_filter(std::unique_ptr<DamageFilter>(invinc));
			}
		}
		else
		{
			if (invinc)
			{
				plr_ent->ref_hlc().rem_filter(invinc);
				invinc = {};
			}
		}
		
		if (plr_ent)
		{
			plr_ent->eqp.infinite_ammo = cheat_ammo || cheat_godmode;
			plr_ent->log.pmov.cheat_infinite = cheat_godmode;
		}
	}
	void step() override
	{
		try_spawn_plr();
		if (plr_ent)
		{
			auto& pcst = PlayerInput::get().get_state(PlayerInput::CTX_GAME);
			plr_ent->log.m_step(PlayerInput::get());
			
			// update lookat string
			
			Entity* lookat = nullptr;
			
			core.get_phy().query_aabb(
			Rectfp::from_center(pcst.tar_pos, vec2fp::one(0.3)),
			[&](Entity& ent, b2Fixture& fix)
			{
				if (typeid(ent) == typeid(EWall)) return true;
				if (fix.IsSensor() && ent.ref_phobj().is_material()) return true;
				
				lookat = &ent;
				return false;
			});
			
			if (lookat) {
				if (lookat->ui_descr) pui_lookat = lookat->ui_descr;
				else pui_lookat = "Unknown";
			}
			else pui_lookat = {};

			// find interactive
			
			Entity* einter = nullptr;
			size_t einter_num = 0;
			
			core.get_phy().query_circle_all( conv(plr_ent->get_pos()), GameConst::hsz_rat + 2,
			[&](auto& ent, auto&) {
				einter = &ent;
				++einter_num;
			},
			[](auto&, auto& fix) {
				auto f = get_info(fix);
				return f && (f->typeflags & FixtureInfo::TYPEFLAG_INTERACTIVE);
			});
			
			if (einter_num == 1)
			{
				auto e = dynamic_cast<EInteractive*>(einter);
				if (!e) THROW_FMTSTR("Entity is not EInteractive - {}", einter->dbg_id());
				
				auto now = core.get_step_time();
				if (pcst.is[ PlayerInput::A_INTERACT ] && now > interact_after)
				{
					e->use(plr_ent);
					interact_after = now + TimeSpan::seconds(0.5);
				}
				
				pui_einter = e->index;
			}
			else pui_einter = {};
		}
		
		for (auto& hack : nethack_server) {
			if (auto ent = core.valid_ent(hack.first)) {
				static_cast<PlayerEntity*>(ent)->log.m_step(*hack.second);
			}
		}
		if (nethack_client) {
			if (auto ent = core.valid_ent(nethack_client->first)) {
				last_plr_pos = ent->get_pos();
			}
		}
	}
	std::pair<Rectfp, Rectfp> get_ai_rects() override
	{
		if (fastforward) {
			auto& lc = core.get_lc();
			Rectfp r = Rectfp::bounds({}, vec2fp(lc.get_size()) * GameConst::cell_size);
			return {r, r};
		}
		
		// halfsizes
		const vec2fp hsz_on = debug_ai_rect ? vec2fp{48, 35} : vec2fp{100, 80};
		const vec2fp hsz_off = hsz_on * 1.25;
		
		if (auto ent = core.get_ent(plr_eid))
			last_plr_pos = ent->get_pos();
		
		vec2fp ctr = last_plr_pos;
		return {
			Rectfp::from_center(ctr, hsz_on),
			Rectfp::from_center(ctr, hsz_off)
		};
	}
	void set_pui(std::unique_ptr<PlayerUI> pui) override
	{
//		if (pui) pui->message("The First and Only Level", TimeSpan::seconds(1));
		this->pui = std::move(pui);
	}
	vec2fp get_last_pos() override
	{
		return last_plr_pos;
	}
	
	
	
	void try_spawn_plr()
	{
	    if (nethack_client) return;
		if (core.get_ent(plr_eid)) return;
		plr_eid = {};
		
		if (plr_ent)
		{
			plr_ent = nullptr;
			invinc = nullptr;
			if (pui)
			{
				auto fade = TimeSpan::seconds(0.5);
				pui->message("YOU DIED", resp_time - fade, fade);
				pui->get_wpnrep().reset();
			}
		}
		
		if (plr_resp.is_positive())
		{
			plr_resp -= GameCore::step_len;
			return;
		}
		
		force_spawn();
	}
	void force_spawn()
	{
		plr_resp = resp_time;
		
		// get spawn pos
		
		vec2fp plr_pos = {};
		for (auto& p : core.get_lc().get_spawns()) {
			if (p.type == LevelControl::SP_PLAYER) {
				plr_pos = p.pos;
				break;
			}
		}
		
		// create
		
		plr_ent = new PlayerEntity(core, plr_pos, is_superman);
		plr_eid = plr_ent->index;
		update_cheats();
		
		if (pui) plr_ent->eqp.msgrep = &pui->get_wpnrep();
	}
	void add_msg(std::string s)
	{
		if (pui) pui->message(std::move(s), TimeSpan::seconds(1.5));
	}
};
PlayerManager* PlayerManager::create(GameCore& core) {
	return new PlayerManager_Impl(core);
}
