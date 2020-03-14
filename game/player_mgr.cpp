#include "client/level_map.hpp"
#include "client/player_ui.hpp"
#include "client/plr_input.hpp"
#include "client/presenter.hpp"
#include "game/game_core.hpp"
#include "game/level_ctr.hpp"
#include "game_objects/objs_basic.hpp"
#include "game_objects/objs_player.hpp"
#include "vaslib/vas_log.hpp"
#include "player_mgr.hpp"



class PlayerManager_Impl : public PlayerManager
{
public:
	const TimeSpan resp_time = TimeSpan::seconds(5);
	
	// control
	
	GameCore& core;
	
	TimeSpan plr_resp; // respawn timeout
	vec2fp last_plr_pos = {}; // for AI rects
	
	// objective
	
	const size_t obj_need = 3;
	size_t obj_count = 0;
	
	EFinalTerminal* obj_term = nullptr;
	bool obj_msg = false;
	bool lct_found = false;
	
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
		if (!core.get_ent(plr_eid))
		{
			plr_ent = nullptr;
			invinc = nullptr;
			return nullptr;
		}
		return plr_ent;
	}
	Entity& ref_ent() override
	{
		if (auto e = get_ent()) return *e;
		THROW_FMTSTR("PlayerManager::ref_ent() failed");
	}
	bool is_player(Entity& ent) const override
	{
		return ent.index == plr_eid;
	}
	
	
	
	void render(TimeSpan passed, vec2i cursor_pos) override
	{
		if (!pui) return;
		
		PlayerUI::DrawState uis;
		uis.resp_left = plr_resp;
		uis.lookat = pui_lookat;
		uis.einter = dynamic_cast<EInteractive*>(core.valid_ent(pui_einter));
		
		if (obj_count < obj_need) {
			uis.objective += FMT_FORMAT("collect security tokens ({} left)", obj_need - obj_count);
		}
		else if (obj_term)
		{
			if (obj_term->timer_end.is_positive())
			{
				TimeSpan left = obj_term->timer_end - core.get_step_time();
				if (left.is_negative()) uis.objective += "use control terminal";
				else uis.objective += FMT_FORMAT("wait for control terminal to boot - {:.1f} seconds", left.seconds());
			}
			else if (!lct_found) uis.objective += "find control terminal";
			else uis.objective += "activate control terminal";
		}
		
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
			plr_ent->log.m_step();
			
			// objective
			
			if (!lct_found)
			{
				auto room = core.get_lc().ref_room(plr_ent->get_pos());
				if (room && room->type == LevelCtrRoom::T_FINAL_TERM)
				{
					lct_found = true;
					add_msg("You have found\ncontrol room");
					LevelMap::get().mark_final_term(*room);
				}
			}
			
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
		
		// objective
		if (!obj_term)
		{
			core.foreach([this](Entity& e)
			{
				if (auto p = dynamic_cast<EFinalTerminal*>(&e))
					obj_term = p;
			});
		}
		if (!obj_msg && obj_term && obj_term->timer_end.is_positive() && obj_term->timer_end < core.get_step_time())
		{
			obj_msg = true;
			add_msg("Terminal is ready");
		}
	}
	std::pair<Rectfp, Rectfp> get_ai_rects() override
	{
		if (fastforward) {
			auto& lc = core.get_lc();
			Rectfp r {{}, vec2fp(lc.get_size()) * GameConst::cell_size, false};
			return {r, r};
		}
		
		// halfsizes
		const vec2fp hsz_on = debug_ai_rect ? vec2fp{48, 35} : vec2fp{80, 65};
		const vec2fp hsz_off = hsz_on * 1.25;
		
		if (auto ent = core.get_ent(plr_eid))
			last_plr_pos = ent->get_pos();
		
		vec2fp ctr = last_plr_pos;
		return {
			{ctr - hsz_on,  ctr + hsz_on,  false},
			{ctr - hsz_off, ctr + hsz_off, false}
		};
	}
	void inc_objective() override
	{
		++obj_count;
		if (obj_count == obj_need)
		{
			if (lct_found) add_msg("Activate control terminal");
			else add_msg("Find control terminal");
			obj_term->enabled = true;
		}
		else /*if (obj_count < 3)*/ // this bug is funny
		{
			size_t n = obj_need - obj_count;
			GamePresenter::get()->add_float_text({ plr_ent->get_pos(), FMT_FORMAT("Security token!\nNeed {} more", n) });
		}
	}
	bool is_game_finished() override
	{
		return obj_term && obj_term->is_activated && obj_term->timer_end < core.get_step_time();
	}
	void on_teleport_activation() override
	{
		if (!lct_found && obj_count >= obj_need)
		{
			lct_found = true;
			add_msg("Map updated.\nActivate control terminal");
			
			LevelMap::get().mark_final_term([&]{
				for (auto& r : core.get_lc().get_rooms()) {
					if (r.type == LevelCtrRoom::T_FINAL_TERM)
						return r;
				}
				throw std::runtime_error("PlayerManager::on_teleport_activation() no final term room");
			}());
		}
	}
	void set_pui(std::unique_ptr<PlayerUI> pui) override
	{
//		if (pui) pui->message("The First and Only Level", TimeSpan::seconds(1));
		this->pui = std::move(pui);
	}
	
	
	
	void try_spawn_plr()
	{
		if (core.get_ent(plr_eid)) return;
		plr_eid = {};
		
		if (plr_ent)
		{
			plr_ent = nullptr;
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
