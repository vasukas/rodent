#include "client/plr_control.hpp"
#include "core/vig.hpp"
#include "render/camera.hpp"
#include "render/control.hpp"
#include "render/ren_imm.hpp"
#include "game_core.hpp"
#include "level_ctr.hpp"
#include "player.hpp"
#include "player_mgr.hpp"
#include "s_objs.hpp"



class PlayerManager_Impl : public PlayerManager
{
public:
	const TimeSpan resp_time = TimeSpan::seconds(3);
	
	// control
	
	std::shared_ptr<PlayerController> pc_ctr;
	TimeSpan plr_resp; // respawn timeout
	vec2i last_plr_pos = {}; // level coords
	
	// objective
	
	const size_t obj_need = 3;
	size_t obj_count = 0;
	EFinalTerminal* obj_term = nullptr;
	bool obj_msg = false;
	bool lct_found = false;
	
	// entity
	
	EntityIndex plr_eid = {};
	PlayerEntity* plr_ent = nullptr;
	
	// interface
	
	struct Message
	{
		std::string s;
		TimeSpan tmo, tmo_dec;
		bool full = true;
		
		Message(std::string s, TimeSpan show, TimeSpan fade)
			: s(std::move(s)), tmo(show), tmo_dec(fade)
		{}
	};
	std::vector<Message> msgs;
	
	
	
	PlayerManager_Impl(std::shared_ptr<PlayerController> pc_ctr_in)
		: pc_ctr(std::move(pc_ctr_in))
	{
		TimeSpan st = TimeSpan::seconds(1.5);
		std::string s = "Press ESCAPE to see controls";
		if (pc_ctr->get_gpad()) {st *= 2; s += "\nPress START to enable gamepad";}
		msgs.emplace_back(std::move(s), st, TimeSpan::seconds(1.5));
	}
	Entity* get_ent() const override
	{
		return plr_ent;
	}
	bool is_player(Entity* ent) const override
	{
		return ent->index == plr_eid;
	}
	
	
	
	void render(TimeSpan passed) override
	{
		if (plr_ent)
		{
			if (god_mode) vig_label("!!! GOD MODE ENABLED !!!\n");
			
			auto plr = static_cast<PlayerEntity*>(plr_ent);
			
			auto& hp = plr_ent->get_hlc()->get_hp();
			vig_progress(FMT_FORMAT("Health {:3}/{}", hp.exact().first, hp.exact().second), hp.t_state());
			vig_lo_next();
			
			vig_progress(FMT_FORMAT("Accel {}", plr->mov.get_t_accel().first? "OK     " : "charge "), 
			             plr->mov.get_t_accel().second);
			vig_lo_next();
			
			{	auto& hp = plr->pers_shld->get_hp();
				vig_progress(FMT_FORMAT("P.shld {:3}/{}", hp.exact().first, hp.exact().second), hp.t_state());
			}
			vig_lo_next();
			
			auto& sh = plr->log.shlc;
			if (sh.is_enabled())
			{
				auto& hp = sh.get_ft()->get_hp();
				vig_progress(FMT_FORMAT("Shield {:3}/{}", hp.exact().first, hp.exact().second), hp.t_state());
			}
			else
			{
				if (auto t = sh.get_dead_tmo()) vig_label_a("Ready in {:.3f}", t->seconds());
				else vig_label("Shield disabled");
			}
			vig_lo_next();
			
			vec2fp pos = plr_ent->get_phy().get_pos();
			vig_label_a("X {:3.3f} Y {:3.3f}\n", pos.x, pos.y);
			
			bool wpn_menu = pc_ctr->get_state().is[PlayerController::A_SHOW_WPNS];
			
			const vec2i el_size = {60, 60};
//			const vec2i el_off = vec2i::one(4);
			
			auto eqp = plr_ent->get_eqp();
			for (auto& wpn : eqp->raw_wpns())
			{
				bool is_cur = wpn.get() == eqp->wpn_ptr();
				if (!is_cur && !wpn_menu) continue;
				
				auto& ri = *wpn->info;
				
				vec2i pos, size = el_size;
				vig_lo_place(pos, size);
				
//				auto mpars = fit_rect( ResBase::get().get_size(ri.model), el_size - el_off );
//				RenAAL::get().draw_inst(Transform{pos + mpars.second}, FColor(), ri.model);
				
				EC_Equipment::Ammo* ammo;
				if (wpn->info->ammo == AmmoType::None) ammo = nullptr;
				else ammo = &eqp->get_ammo(wpn->info->ammo);
				
				uint32_t clr;
				if (is_cur)
				{
					if (ammo && !eqp->has_ammo(*wpn)) clr = 0xff0000ff;
					else clr = 0x00ff00ff;
				}
				else if (ammo && !eqp->has_ammo(*wpn)) clr = 0xff0000ff;
				else clr = 0xff8000ff;
				RenImm::get().draw_frame(Rectfp{pos, el_size, true}, clr, 2);
				
				std::string s;
				s.reserve(40);
				
				s += ri.name;
				
				if (ammo)
				{
					if (eqp->infinite_ammo) s += "\nAMMO CHEAT ENABLED";
					else s += FMT_FORMAT("\nAmmo: {} / {}", int_round(ammo->value), int_round(ammo->max));
				}
				if (is_cur)
				{
					if (auto& m = wpn->overheat)
					{
						if (!m->is_ok()) s += "\nCOOLDOWN";
						else if (m->value > 0.5) s += "\nOverheat";
						else s += "\n ";
					}
					if (auto m = wpn->info->def_delay; m && m > TimeSpan::seconds(0.5))
					{
						if (wpn->get_reload_timeout())
							s += "\nReload";
					}
					if (auto m = wpn->get_ui_info())
					{
						if (m->charge_t)
							s += FMT_FORMAT("\nCharge: {:3}%", int_round(*m->charge_t * 100));
					}
				}
				
				vig_label(s);
				vig_lo_next();
			}
		}
		
		if (!msgs.empty())
		{
			auto it = &msgs.back();
			
			uint32_t clr = 0xffffff00;
			if (it->full) clr |= 0xff;
			else clr |= int_round( it->tmo / it->tmo_dec * 255 );
			RenImm::get().draw_text(RenderControl::get_size() /2, it->s, clr, true, 4.f);
			
			it->tmo -= passed;
			if (it->tmo.is_negative())
			{
				if (it->full) {
					it->full = false;
					it->tmo += it->tmo_dec;
				}
				else msgs.pop_back();
			}
		}
		
		if (plr_ent)
		{
			std::string stat_str;
			
			if (auto res = GameCore::get().get_phy().point_cast(conv( pc_ctr->get_state().tar_pos ), 0.5))
			{
				auto s = res->ent->ui_descr();
				if (s.empty()) s = "UNDEFINED";
				stat_str += FMT_FORMAT("Looking at: {}\n", s);
			}
			
			auto room = LevelControl::get().get_room(plr_ent->get_pos());
			stat_str += FMT_FORMAT("Room: {}\n", room? room->name : "Corridor");
			if (!lct_found && room && room->is_final_term)
			{
				lct_found = true;
				add_msg("You have found\nlevel control room");
			}
			
			if (obj_count < obj_need)
				stat_str += FMT_FORMAT("Objective: collect security tokens ({} left)", obj_need - obj_count);
			else if (obj_term)
			{
				if (obj_term->timer_end.is_positive())
				{
					TimeSpan left = obj_term->timer_end - GameCore::get().get_step_time();
					if (left.is_negative()) stat_str += "Objective: use level control terminal";
					else stat_str += FMT_FORMAT("Objective: wait for level control terminal to boot - {:.1f} seconds", left.seconds());
				}
				else if (!lct_found) stat_str += "Objective: find level control terminal";
				else stat_str += "Objective: activate level control terminal";
			}
			RenImm::get().draw_text_hud({0, -1}, stat_str);
			
			//
			
			std::vector<PhysicsWorld::CastResult> res;
			GameCore::get().get_phy().circle_cast_all(res, conv(plr_ent->get_pos()), GameConst::hsz_rat + 2,
			{[&](auto, b2Fixture* f){return getptr(f) && (getptr(f)->typeflags & FixtureInfo::TYPEFLAG_INTERACTIVE);}, {}, false});
			
			if (res.size() == 1)
			{
				auto e = dynamic_cast<EInteractive*>(res[0].ent);
				if (!e) THROW_FMTSTR("Entity is not EInteractive - {}", res[0].ent->dbg_id());

				auto usage = e->use_string();
				RenImm::get().draw_text(
					RenderControl::get().get_world_camera()->direct_cast(e->get_pos()),
					usage.second, usage.first? 0xffffffff : 0xff6060ff, true);
				
				if (pc_ctr->get_state().is[ PlayerController::A_INTERACT ])
					e->use(plr_ent);
			}
		}
		else {
			RenImm::get().draw_text_hud({0, -1}, FMT_FORMAT("Rematerialization in {:.1f} seconds", plr_resp.seconds()));
		}
	}
	void update_godmode() override
	{
		if (god_mode)
		{
			if (!plr_ent) force_spawn();
			
			static_cast<PlayerEntity*>(plr_ent)->mov.accel_inf = true;
			plr_ent->get_eqp()->infinite_ammo = true;
			plr_ent->get_hlc()->add_filter(std::make_shared<DmgIDDQD>());
		}
		else
		{
			if (!plr_ent) return;
			
			static_cast<PlayerEntity*>(plr_ent)->mov.accel_inf = false;
			plr_ent->get_eqp()->infinite_ammo = false;
			
			auto& fs = plr_ent->get_hlc()->raw_fils();
			for (auto it = fs.begin(); it != fs.end(); ++it)
			{
				if (*it && dynamic_cast<DmgIDDQD*>(it->get()))
				{
					fs.erase(it);
					break;
				}
			}
		}
		
		if (plr_ent)
			plr_ent->get_eqp()->infinite_ammo = true;
	}
	void step() override
	{
		if (!obj_term)
		{
			GameCore::get().get_phy().post_step([this]
			{
				auto pos = LevelControl::get().get_closest(LevelControl::SP_FINAL_TERMINAL, {});
				obj_term = new EFinalTerminal(pos);
			});
		}
		
		try_spawn_plr();
		
		if (!obj_msg && obj_term && obj_term->timer_end.is_positive() && obj_term->timer_end < GameCore::get().get_step_time())
		{
			obj_msg = true;
			add_msg("Terminal is ready");
		}
	}
	std::pair<Rect, Rect> get_ai_rects() override
	{
		// halfsizes
		const vec2i hsz_on  = (vec2fp(30, 25) / LevelControl::get().cell_size).int_round();
		const vec2i hsz_off = (vec2fp(45, 37) / LevelControl::get().cell_size).int_round();
		
		if (auto ent = GameCore::get().get_ent(plr_eid))
			last_plr_pos = (ent->get_pos() / LevelControl::get().cell_size).int_round();
		
		vec2i ctr = last_plr_pos;
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
			if (lct_found) add_msg("Go to level control terminal");
			else add_msg("Find level control terminal");
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
		return obj_term && obj_term->is_activated && obj_term->timer_end < GameCore::get().get_step_time();
	}
	
	
	
	void try_spawn_plr()
	{
		if (GameCore::get().get_ent(plr_eid)) return;
		plr_eid = {};
		
		if (plr_ent)
		{
			auto fade = TimeSpan::seconds(0.5);
			msgs.emplace_back("YOU DIED", resp_time - fade, fade);
			plr_ent = nullptr;
		}
		
		{	auto lock = pc_ctr->lock();
			pc_ctr->update();
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
		for (auto& p : LevelControl::get().get_spawns()) {
			if (p.type == LevelControl::SP_PLAYER) {
				plr_pos = p.pos;
				break;
			}
		}
		
		// create
		
		plr_ent = new PlayerEntity (plr_pos, pc_ctr);
		plr_eid = plr_ent->index;
		update_godmode();
	}
	void add_msg(std::string s)
	{
		msgs.emplace_back(std::move(s), TimeSpan::seconds(1.5), TimeSpan::seconds(1.5));
	}
};
PlayerManager* PlayerManager::create(std::shared_ptr<PlayerController> pc_ctr) {
	return new PlayerManager_Impl (std::move(pc_ctr));
}
