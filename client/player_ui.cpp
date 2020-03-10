#include "client/plr_input.hpp"
#include "core/settings.hpp"
#include "core/vig.hpp"
#include "game/level_ctr.hpp"
#include "game/game_core.hpp"
#include "game/player_mgr.hpp"
#include "game/weapon.hpp"
#include "game_objects/objs_basic.hpp"
#include "game_objects/objs_player.hpp"
#include "render/camera.hpp"
#include "render/control.hpp"
#include "render/ren_imm.hpp"
#include "utils/time_utils.hpp"
#include "vaslib/vas_log.hpp"
#include "player_ui.hpp"



class PlayerUI_Impl : public PlayerUI
{
public:
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
	
	// interface - weapons
	
	struct WpnMsgRep : WeaponMsgReport
	{
		TimeSpan time; // since start
		std::string str;
		SmoothBlink no_ammo_tcou;
		
		void jerr(JustError err) {
			time = TimeSpan::since_start();
			switch(err)
			{
			case ERR_SELECT_OVERHEAT:
				str = "Overheated";
				break;
			case ERR_SELECT_NOAMMO:
				str = "No ammo";
				no_ammo_tcou.trigger();
				break;
			case ERR_NO_TARGET:
				str = "No target";
				break;
			}
		}
		void no_ammo(int required) {
			time = TimeSpan::since_start();
			str = FMT_FORMAT("Need {} ammo", required);
		}
		void reset() {
			str = {};
		}
	};
	
	TimeSpan wpn_ring_reload;
	TimeSpan wpn_ring_charge;
	
	WpnMsgRep wpn_msgrep;
	
	// indicator array
	
	struct StatusIndicator
	{
		SmoothBlink blink, blink_upd;
		FColor clr, alt_clr;
		float thr = 0.5; // blink below this value
		float prev_value = 0; // for info only
		
		void draw(std::string_view label, float value, bool use_alt_clr = false)
		{
			prev_value = value;
			
			vig_push_palette();
			float t = blink.get_sine(value < thr && !use_alt_clr);
			
			FColor blc = FColor(1,1,0);
			blc *= 0.7 * blink_upd.get_blink();
			
			vig_CLR(Back)  = (((use_alt_clr ? alt_clr : clr) * 0.7 + -0.2 + blc) * t).to_px();
			vig_CLR(Active) = ((use_alt_clr ? alt_clr : clr) * t + blc).to_px();
			
			vig_progress(label, value);
			vig_pop_palette();
		}
	};
	struct WeaponIndicator
	{
		SmoothBlink blink;
		TimeSpan t_label;
		int ammo_value = -1;
		bool is_cur = false;
		
		void draw(Weapon& wpn, EC_Equipment& eqp, size_t index)
		{
			const TimeSpan label_period = TimeSpan::seconds(2);
			const vec2i el_size = {60, 60};
			const vec2i el_off = vec2i::one(4);
			const int el_y_space = 4;
			
			const int frame_width = 2;
			const int strip_ht = 12;
			
			vec2i pos, size = el_size;
			vig_lo_place(pos, size);
			
			// prepare stats
			
			bool is_cur_now = (index == eqp.wpn_index());
			if (is_cur_now && !is_cur) t_label = label_period;
			is_cur = is_cur_now;
			
			auto& ri = *wpn.info;
			EC_Equipment::Ammo* ammo;
			if (wpn.info->ammo == AmmoType::None) ammo = nullptr;
			else ammo = &eqp.get_ammo(ri.ammo);
			
			// draw helpers
			
			uint32_t frame_clr = [&]{
				return is_cur ? 0x00ff00ffu : 0xff0000ffu;
			}();
			uint32_t bg_clr = [&]
			{
				const float a = 0.7;
				if (ammo) {
					if (!ammo->value) {
		// hack to hide strip
						ammo = nullptr;
						blink.force_reset();
						return FColor(0.7, 0, 0, a).to_px();
					}
					if (ammo_value < ammo->value) blink.trigger();
					ammo_value = ammo->value;
					return (FColor(1, 1, 0, a) * blink.get_blink()).to_px();
				}
				return uint32_t(a * 255);
			}();
			auto draw_strip = [&](float t, uint32_t clr1, uint32_t clr2, int yn)
			{
				t = clampf_n(t);
				
				vec2i p (frame_width, 0);
				vec2i z ((el_size.x - frame_width*2) * t, strip_ht);
				
				if (yn < 0) p.y = el_size.y - strip_ht*-yn - frame_width;
				else p.y = frame_width + strip_ht*yn;
				
				RenImm::get().draw_rect(Rectfp(pos + p, z, true), clr1);
				p.x += z.x;
				z.x = (el_size.x - frame_width*2) - z.x;
				RenImm::get().draw_rect(Rectfp(pos + p, z, true), clr2);
			};
			std::vector<std::pair<FColor, std::string>> ss;
			
			// draw background
			
			RenImm::get().draw_rect(Rectfp{pos, el_size, true}, bg_clr);
			
			{	vec2fp img_size = ResBase::get().get_size(ri.model).size();
				float img_k = img_size.x / img_size.y;
				
				if (img_k > 1) img_size = {1, 1 / img_k};
				else           img_size = {img_k, 1};
				img_size *= el_size/2 - el_off;
			
				RenImm::get().draw_image(Rectfp::from_center( pos + el_size/2, img_size ),
				                         ResBase::get().get_image(ri.model));
			}
			
			// status
			
			if (t_label.is_positive())
			{
				float a = t_label / label_period;
				ss.emplace_back(FColor(1,1,1,a).to_px(), ri.name);
				t_label -= RenderControl::get().get_passed();
			}
			ss.emplace_back(FColor{}, "\n");
			
			if (ammo && !eqp.infinite_ammo)
			{
				if (!ammo->value) draw_strip(1, 0xff0000ff, 0, -1);
				else draw_strip(float(ammo->value)/ammo->max, 0xc0e0e0ff, 0, -1);
			}
			
			if (auto& m = wpn.overheat)
			{
				if (!m->is_ok()) {
					ss.emplace_back(FColor(1, 0.8, 0.5), "COOLDOWN");
					draw_strip(m->value, 0xff4040ff, 0, 0);
				}
				else {
					if (m->value > 0.5) ss.emplace_back(FColor(1, 1, 0), "Overheat");
					draw_strip(m->value, 0xc0c000ff, 0, 0);
				}
				ss.emplace_back(FColor{}, "\n");
			}
			if (auto m = wpn.info->def_delay; m && m > TimeSpan::seconds(0.5))
			{
				if (wpn.get_reload_timeout())
					ss.emplace_back(FColor(0,1,0), "Reload");
			}
			if (auto m = wpn.get_ui_info())
			{
				if (m->charge_t) {
					ss.emplace_back(FColor(0.5, 1, 1), FMT_FORMAT("Charge: {:3}%", int_round(*m->charge_t * 100)));
					draw_strip(*m->charge_t, 0xc0c0ffff, 0xff000080, wpn.overheat ? 1 : 0);
				}
			}
			
			//
			
			RenImm::get().draw_frame(Rectfp{pos, el_size, true}, frame_clr, frame_width);
			RenImm::get().draw_text(pos + el_off, std::to_string(index + 1));
			RenImm::get().draw_text(pos + el_off + vec2i(el_size.x, 0), std::move(ss));
			vig_space_line(el_y_space);
		}
	};
	
	std::vector<StatusIndicator> ind_stat;
	std::vector<WeaponIndicator> ind_wpns;
	
	enum IndStat {
		INDST_HEALTH,
		INDST_ACCEL,
		INDST_ARMOR,
		INDST_PERS_SHIELD,
		INDST_PROJ_SHIELD,
		INDST_PROJ_SHIELD_REGEN,
		INDST_TOTAL_COUNT_INTERNAL
	};
	
	bool proj_shld_was_dead = false;
	
	
	
	PlayerUI_Impl()
	{
		ind_stat.resize(INDST_TOTAL_COUNT_INTERNAL);
		//
		ind_stat[INDST_HEALTH].clr = FColor(0x10c060ff);
		ind_stat[INDST_ACCEL].clr     = FColor(0xd07010ff);
		ind_stat[INDST_ACCEL].alt_clr = FColor(0x706060ff);
		//
		ind_stat[INDST_ARMOR].clr = FColor(0x10c000ff);
		ind_stat[INDST_ARMOR].thr = 0;
		ind_stat[INDST_PERS_SHIELD].clr = FColor(0x4040d0ff);
		//
		ind_stat[INDST_PROJ_SHIELD].clr     = FColor(0x40d0d0ff);
		ind_stat[INDST_PROJ_SHIELD].alt_clr = FColor(0x30a0a0ff);
		ind_stat[INDST_PROJ_SHIELD].thr = 0.33;
		//
		ind_stat[INDST_PROJ_SHIELD_REGEN].clr     = FColor(0xa02020ff);
		ind_stat[INDST_PROJ_SHIELD_REGEN].alt_clr = FColor(0x289090ff);
		ind_stat[INDST_PROJ_SHIELD_REGEN].thr = 1;
	}
	void render(PlayerManager& mgr, const DrawState& dstate, TimeSpan passed, vec2i mou_pos)
	{
		auto plr_ent = static_cast<PlayerEntity*>(mgr.get_ent());
		
		if (!plr_ent)
		{
			for (auto& i : ind_stat) {
				i.blink.force_reset();
				i.blink_upd.force_reset();
			}
			ind_wpns.clear();
		}
		
		if (plr_ent)
		{
			if (mgr.cheat_godmode) vig_label("!!! GOD MODE ENABLED !!!\n");
			
			auto& hlc = plr_ent->hlc;
			auto& log = plr_ent->log;
			
			// Health
			{	auto& hp = hlc.get_hp();
				ind_stat[INDST_HEALTH].draw(FMT_FORMAT("Health {:3}/{}", hp.exact().first, hp.exact().second), hp.t_state());
			}
			vig_lo_next();
			
			// Accelartion
			if (log.pmov.is_infinite_accel()) {
				ind_stat[INDST_ACCEL].draw("Accel INFINIT", 1, true);
			}
			else {
				auto acc_st = log.pmov.get_t_accel();
				ind_stat[INDST_ACCEL].draw(FMT_FORMAT("Accel {}", acc_st.first? "OK     " : "charge "),
									       acc_st.second, !acc_st.first);
			}
			vig_lo_next();
			
			// Armor
			{	auto& hp = log.armor->get_hp();
				auto& ind = ind_stat[INDST_ARMOR];
				if (hp.t_state() > ind.prev_value) ind.blink_upd.trigger();
				ind.draw(FMT_FORMAT("Armor {:3}/{}", hp.exact().first, hp.exact().second), hp.t_state());
			}
			vig_lo_next();
			
			// Shield
			{	auto& hp = log.pers_shld->get_hp();
				ind_stat[INDST_PERS_SHIELD].draw(FMT_FORMAT("P.shld {:3}/{}", hp.exact().first, hp.exact().second), hp.t_state());
			}
			vig_lo_next();
			
			// Projected shield
			{
				auto& sh = log.shlc;
				auto& i_alive = ind_stat[INDST_PROJ_SHIELD];
				auto& i_regen = ind_stat[INDST_PROJ_SHIELD_REGEN];
				auto st = sh.get_state();
				
				switch (st.first)
				{
				case ShieldControl::ST_DEAD: {
						proj_shld_was_dead = true;
						i_regen.draw("Regenerating...", st.second / sh.dead_time);
					}
					break;
					
				case ShieldControl::ST_DISABLED: {
						if (std::exchange(proj_shld_was_dead, false)) {
							i_alive.blink.force_reset();
							i_alive.blink.trigger();
						}
						i_alive.draw("Shield - ready", 0, true);
					}
					break;
					
				case ShieldControl::ST_SWITCHING: {
						i_regen.draw("Shield        ", 1, true);
					}
					break;
					
				case ShieldControl::ST_ACTIVE: {
						auto& hp = sh.get_ft()->get_hp();
						i_alive.draw(FMT_FORMAT("Shield {:3}/{}", hp.exact().first, hp.exact().second), hp.t_state());
					}
					break;
				}
			}
			vig_lo_next();
			
//			vec2fp pos = plr_ent->get_phy().get_pos();
//			vig_label_a("X {:3.3f} Y {:3.3f}\n", pos.x, pos.y);
			
			// Weapons
			
			auto& eqp = plr_ent->eqp;
			
			{	auto& wpn = eqp.get_wpn();
				auto& ammo = eqp.get_ammo(wpn.info->ammo);
				vig_label_a("{}\nAmmo: {} / {}\n", wpn.info->name, ammo.value, ammo.max);
			}
			{	auto& wpns = eqp.raw_wpns();
				ind_wpns.resize( wpns.size() );
				
				for (size_t i=0; i < wpns.size(); ++i)
					ind_wpns[i].draw( *wpns[i], eqp, i );
			}
			
			const TimeSpan wmr_max = TimeSpan::seconds(1.5);
			TimeSpan wmr_passed = TimeSpan::since_start() - wpn_msgrep.time;
			if (wmr_passed < wmr_max)
			{
				uint32_t clr = 0xffffff00 | lerp(0, 255, 1 - wmr_passed / wmr_max);
				RenImm::get().draw_text(vec2fp(mou_pos.x, mou_pos.y + 30), wpn_msgrep.str, clr, true);
			}
			if (!eqp.has_ammo( eqp.get_wpn() ))
				RenImm::get().draw_text(vec2fp(mou_pos.x, mou_pos.y - 30), "Ammo: 0", 0xffff'ffc0, true);
			
			// Cursor
			
//			int cursor_rings_x = 0;
			if (AppSettings::get().cursor_info_flags & 1)
			{
				float radius = 20;
				const float width = 6;
				const int alpha = 0xc0;
				
				const TimeSpan hide = TimeSpan::seconds(2);
				const TimeSpan fade = TimeSpan::seconds(0.4);
				TimeSpan now = TimeSpan::since_start();
				
				//
				
				auto& wpn = eqp.get_wpn();
				
				auto& ammo_tcou = wpn_msgrep.no_ammo_tcou;
				if (!eqp.has_ammo(wpn)) ammo_tcou.trigger();
				float ammo_t = ammo_tcou.get_blink();
				
				if (ammo_t > 0.01)
				{
					float t = std::fmod(now.seconds(), 1);
					if (t > 0.5) t = 1 - t;
					t *= 2;
					
					uint32_t clr = 0xff000000 | int(alpha * ammo_t);
					clr += lerp<int>(64, 220, t) << 16;
					clr += lerp<int>(64, 220, t) << 8;
					
					draw_progress_ring(mou_pos, 1, clr, radius, width);
					radius += width;
				}
				else
				{
					if (auto m = wpn.info->def_delay; m && *m > TimeSpan::seconds(0.5))
					{
						uint32_t clr = alpha; float t;
						if (auto tmo = wpn.get_reload_timeout()) {
							clr |= 0xc0ff4000;
							t = 1 - *tmo / *m;
							wpn_ring_reload = now;
						}
						else {
							auto diff = now - wpn_ring_reload;
							if (diff > hide) clr = alpha * clampf_n(1. - (diff - hide) / fade);
							clr |= 0x40ff4000;
							t = 1;
						}
						draw_progress_ring(mou_pos, t, clr, radius, width);
						radius += width;
					}
					if (auto& m = wpn.overheat)
					{
						uint32_t clr = m->flag ? 0xff606000 : 0xffff0000;
						draw_progress_ring(mou_pos, m->value, clr | alpha, radius, width);
						radius += width;
					}
					if (auto m = wpn.get_ui_info())
					{
						if (m->charge_t)
						{
							uint32_t clr = alpha;
							if (*m->charge_t > 0.99) {
								auto diff = now - wpn_ring_charge;
								if (diff > hide) clr = alpha * clampf_n(1. - (diff - hide) / fade);
								clr |= 0xe0ffff00;
							}
							else {
								clr |= 0x40c0ff00;
								wpn_ring_charge = now;
							}
							draw_progress_ring(mou_pos, *m->charge_t, clr, radius, width);
						}
						radius += width;
					}
				}
//				cursor_rings_x = radius;
			}
		}
		
		if (!msgs.empty())
		{
			auto it = &msgs.back();
			
			uint32_t clr = 0xffffff00;
			vec2fp off = {};
			float szk = 4.f;
			
			if (it->full) clr |= 0xff;
			else {
				const float thr = 0.5;
				float t = it->tmo / it->tmo_dec;
				
				clr |= int_round(std::max(t, thr) * 255);
				if (t < thr) {
					t /= thr;
					off.x = -(1 - t);
					off.y = (1 - t);
					szk *= t;
				}
			}
			draw_text_message(it->s, szk, clr, off * RenderControl::get_size() /2);
			
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
			auto& pinp = PlayerInput::get();
			
			std::string stat_str;
			stat_str += FMT_FORMAT("Looking at: {}\n", dstate.lookat);
			
			auto& lc = plr_ent->core.get_lc();
			auto room = lc.ref_room(plr_ent->get_pos());
			stat_str += FMT_FORMAT("Room: {}\n", room? room->name : "Corridor");
			
			if (auto c = lc.cell( lc.to_cell_coord( pinp.get_state(PlayerInput::CTX_GAME).tar_pos ) ))
				stat_str += FMT_FORMAT("{} {}x{}\n", c->is_wall ? "Wall" : "Cell", c->pos.x, c->pos.y);
			
			stat_str += FMT_FORMAT("Objective: {}", dstate.objective);
			draw_text_hud({0, -1}, stat_str);
			
			//
			
			if (auto e = dstate.einter)
			{
				auto usage = e->use_string();
				
				std::string str;
				if (usage.first) str = FMT_FORMAT("[{}] ", pinp.get_hint( PlayerInput::A_INTERACT ));
				str += usage.second;
				
				RenImm::get().draw_text(
					RenderControl::get().get_world_camera().direct_cast(e->get_pos()),
					str, usage.first? 0xffffffff : 0xff6060ff, true
				);
			}
		}
		else {
			draw_text_hud({0, -1}, FMT_FORMAT("Rematerialization in {:.1f} seconds", dstate.resp_left.seconds()));
		}
		
		// shouldn't be visible by player
		RenImm::get().set_context(RenImm::DEFCTX_WORLD);
		auto [ai_on, ai_off] = mgr.get_ai_rects();
		RenImm::get().draw_frame(ai_on,  0x00ff0060, 0.5);
		RenImm::get().draw_frame(ai_off, 0xff000060, 0.5);
		RenImm::get().set_context(RenImm::DEFCTX_UI);
	}
	WeaponMsgReport& get_wpnrep()
	{
		return wpn_msgrep;
	}
	void message(std::string s, TimeSpan show, TimeSpan fade)
	{
		msgs.emplace_back(std::move(s), show, fade);
	}
};
PlayerUI* PlayerUI::create() {
	return new PlayerUI_Impl;
}
