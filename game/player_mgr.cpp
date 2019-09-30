#include "client/plr_control.hpp"
#include "core/vig.hpp"
#include "render/control.hpp"
#include "render/ren_imm.hpp"
#include "game_core.hpp"
#include "level_ctr.hpp"
#include "player.hpp"
#include "player_mgr.hpp"



class PlayerManager_Impl : public PlayerManager
{
public:
	const TimeSpan resp_time = TimeSpan::seconds(3);
	
	// control
	
	std::shared_ptr<PlayerController> pc_ctr;
	TimeSpan plr_resp; // respawn timeout
	
	// entity
	
	EntityIndex plr_eid = {};
	PlayerEntity* plr_ent = nullptr;
	
	// average velocity
	
	std::vector<vec2fp> av_pos;
	size_t av_index = 0;
	vec2fp plr_avel = {};
	
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
	
	
	
	PlayerManager_Impl(std::shared_ptr<PlayerController> pc_ctr)
		: pc_ctr(std::move(pc_ctr))
	{
		av_pos.resize(TimeSpan::seconds(1) / GameCore::step_len, vec2fp{});
		
//		TimeSpan st = TimeSpan::seconds(1.5);
//		std::string s = "Press ESCAPE to see controls";
//		if (pc_ctr->get_gpad()) {st *= 2; s += "\nPress START to enable gamepad";}
//		msgs.emplace_back(std::move(s), st, TimeSpan::seconds(1.5));
	}
	Entity* get_ent() const override
	{
		return plr_ent;
	}
	bool is_player(Entity* ent) const override
	{
		return ent->index == plr_eid;
	}
	vec2fp get_avg_vel() const override
	{
		return plr_avel;
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
	}
	void step() override
	{
		try_spawn_plr();
		
		if (plr_ent)
		{
			auto vel = plr_ent->get_phy().get_vel().pos;
			av_pos[av_index % av_pos.size()] = vel;
			++av_index; // overflow is ok
			
			plr_avel = {};
			for (auto& p : av_pos) plr_avel += p;
			plr_avel /= av_pos.size();
		}
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
};
PlayerManager* PlayerManager::create(std::shared_ptr<PlayerController> pc_ctr) {
	return new PlayerManager_Impl (std::move(pc_ctr));
}
