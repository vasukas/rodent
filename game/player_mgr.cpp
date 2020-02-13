#include "client/level_map.hpp"
#include "client/plr_control.hpp"
#include "core/settings.hpp"
#include "core/vig.hpp"
#include "game_objects/objs_creature.hpp"
#include "game_objects/player.hpp"
#include "render/camera.hpp"
#include "render/control.hpp"
#include "render/ren_imm.hpp"
#include "vaslib/vas_log.hpp"
#include "game_core.hpp"
#include "level_ctr.hpp"
#include "player_mgr.hpp"



class PlayerManager_Impl : public PlayerManager
{
public:
	const TimeSpan resp_time = TimeSpan::seconds(5);
	
	// control
	
	std::shared_ptr<PlayerController> pc_ctr;
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
	
	TimeSpan interact_after;
	
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
				str = "Can't switch: Overheated";
				break;
			case ERR_SELECT_NOAMMO:
				str = "Can't switch: No ammo";
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
	
	
	
	PlayerManager_Impl(std::shared_ptr<PlayerController> pc_ctr_in)
		: pc_ctr(std::move(pc_ctr_in))
	{
		TimeSpan st = TimeSpan::seconds(1);
		std::string s = "The First and Only Level";
		if (pc_ctr->get_gpad()) {st *= 2; s += "\nPress START to enable gamepad";}
		msgs.emplace_back(std::move(s), st, TimeSpan::seconds(1.5));
		
		if (PlayerEntity::is_superman)
			cheat_godmode = true;
		
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
		ind_stat[INDST_PROJ_SHIELD_REGEN].clr = FColor(0xa02020ff);
		ind_stat[INDST_PROJ_SHIELD_REGEN].thr = 1;
	}
	Entity* get_ent() override
	{
		if (!GameCore::get().get_ent(plr_eid)) {
			plr_ent = nullptr;
			return nullptr;
		}
		return plr_ent;
	}
	bool is_player(Entity* ent) const override
	{
		return ent->index == plr_eid;
	}
	
	
	
	void render(TimeSpan passed, vec2i mou_pos) override
	{
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
			if (cheat_godmode) vig_label("!!! GOD MODE ENABLED !!!\n");
			
			auto plr = static_cast<PlayerEntity*>(plr_ent);
			
			// Health
			{	auto& hp = plr_ent->get_hlc()->get_hp();
				ind_stat[INDST_HEALTH].draw(FMT_FORMAT("Health {:3}/{}", hp.exact().first, hp.exact().second), hp.t_state());
			}
			vig_lo_next();
			
			// Accelartion
			if (plr->mov.is_infinite_accel()) {
				ind_stat[INDST_ACCEL].draw("Accel INFINIT", 1, true);
			}
			else {
				auto acc_st = plr->mov.get_t_accel();
				ind_stat[INDST_ACCEL].draw(FMT_FORMAT("Accel {}", acc_st.first? "OK     " : "charge "),
									       acc_st.second, !acc_st.first);
			}
			vig_lo_next();
			
			// Armor
			{	auto& hp = plr->armor->get_hp();
				auto& ind = ind_stat[INDST_ARMOR];
				if (hp.t_state() > ind.prev_value) ind.blink_upd.trigger();
				ind.draw(FMT_FORMAT("Armor {:3}/{}", hp.exact().first, hp.exact().second), hp.t_state());
			}
			vig_lo_next();
			
			// Shield
			{	auto& hp = plr->pers_shld->get_hp();
				ind_stat[INDST_PERS_SHIELD].draw(FMT_FORMAT("P.shld {:3}/{}", hp.exact().first, hp.exact().second), hp.t_state());
			}
			vig_lo_next();
			
			// Projected shield
			{	auto& sh = plr->log.shlc;
				auto& ind = ind_stat[INDST_PROJ_SHIELD];
				if (sh.is_enabled())
				{
					auto& hp = sh.get_ft()->get_hp();
					ind.draw(FMT_FORMAT("Shield {:3}/{}", hp.exact().first, hp.exact().second), hp.t_state());
				}
				else
				{
					if (auto t = sh.get_dead_tmo()) {
						proj_shld_was_dead = true;
						ind_stat[INDST_PROJ_SHIELD_REGEN].draw("Regenerating...", *t / sh.dead_regen_time);
					}
					else {
						if (std::exchange(proj_shld_was_dead, false)) {
							ind.blink.force_reset();
							ind.blink.trigger();
						}
						ind.draw("Shield - ready", 0, true);
					}
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
			
			// Cursor
			
			int cursor_rings_x = 0;
			if (AppSettings::get().cursor_info_flags & 1)
			{
				float radius = 20;
				const float width = 6;
				const int alpha = 0xc0;
				
				const TimeSpan hide = TimeSpan::seconds(2);
				const TimeSpan fade = TimeSpan::seconds(0.4);
				TimeSpan now = GameCore::get().get_step_time();
				
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
				cursor_rings_x = radius;
			}
			
			if (AppSettings::get().cursor_info_flags & 6)
			{
				const float hlen = 12; // half height
				const float width = 6;
				const float space = 1;
				const float frame = 2;
				const int alpha = 0xc0;
				int xoff = std::max(cursor_rings_x + 5, 10);
				
				auto rect = [&](int num, float t, uint32_t clr)
				{
					if (num > 0) --num;
					float x = xoff + (width + frame*2 + space) * std::abs(num);
					if (num < 0) x = -x;
					x += mou_pos.x;
					
					float y = mou_pos.y - (hlen + frame);
					RenImm::get().draw_frame({{x,y}, {width + frame*2, (hlen + frame)*2}, true}, clr, frame);
					
					y = 2*hlen * (1 - t);
					RenImm::get().draw_rect({{x + frame, mou_pos.y - hlen + y}, {width, hlen*2 - y}, true}, clr);
				};
				
				if (AppSettings::get().cursor_info_flags & 2)
				{
					rect(-1, plr->pers_shld->get_hp().t_state(), 0x40c0ff00 | alpha);
					
					auto& sh = plr->log.shlc;
					if (sh.is_enabled()) rect(1, sh.get_ft()->get_hp().t_state(), 0x40c0ff00 | alpha);
					else rect(1, 1, (sh.get_dead_tmo() ? 0xff404000 : 0x40ff4000) | alpha);
				}
				if (AppSettings::get().cursor_info_flags & 4)
				{
					rect(-2, plr_ent->get_hlc()->get_hp().t_state(), 0x40ffc000 | alpha);
					
					auto ta = plr->mov.get_t_accel();
					rect(2, ta.second, (ta.first ? 0x4040ff00 : 0xc0606000) | alpha);
				}
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
			std::string stat_str;
			Entity* lookat = nullptr;
			
			GameCore::get().get_phy().query_aabb(
				Rectfp::from_center(pc_ctr->get_state().tar_pos, vec2fp::one(0.3)),
			[&](Entity& ent, b2Fixture& fix)
			{
				if (typeid(ent) == typeid(EWall)) return true;
				if (fix.IsSensor())
				{
					auto f = ent.get_phobj().body->GetFixtureList();
					for (; f; f = f->GetNext())
						if (!f->IsSensor())
							return true;
				}
				lookat = &ent;
				return false;
			});
			
			if (lookat)
			{
				auto s = lookat->ui_descr();
				if (s.empty()) s = "UNDEFINED";
				stat_str += FMT_FORMAT("Looking at: {}\n", s);
			}
			else stat_str = "Looking at:\n";
			
			auto room = LevelControl::get().ref_room(plr_ent->get_pos());
			stat_str += FMT_FORMAT("Room: {}\n", room? room->name : "Corridor");
			if (!lct_found && room && room->is_final_term)
			{
				lct_found = true;
				add_msg("You have found\ncontrol room");
				LevelMap::get().mark_final_term();
			}
			
			auto& lc = LevelControl::get();
			if (auto c = lc.cell( lc.to_cell_coord( pc_ctr->get_state().tar_pos ) ))
				stat_str += FMT_FORMAT("{} {}x{}\n", c->is_wall ? "Wall" : "Cell", c->pos.x, c->pos.y);
			
			if (obj_count < obj_need)
				stat_str += FMT_FORMAT("Objective: collect security tokens ({} left)", obj_need - obj_count);
			else if (obj_term)
			{
				if (obj_term->timer_end.is_positive())
				{
					TimeSpan left = obj_term->timer_end - GameCore::get().get_step_time();
					if (left.is_negative()) stat_str += "Objective: use control terminal";
					else stat_str += FMT_FORMAT("Objective: wait for control terminal to boot - {:.1f} seconds", left.seconds());
				}
				else if (!lct_found) stat_str += "Objective: find control terminal";
				else stat_str += "Objective: activate control terminal";
			}
			draw_text_hud({0, -1}, stat_str);
			
			//
			
			Entity* einter = nullptr;
			size_t einter_num = 0;
			
			GameCore::get().get_phy().query_circle_all( conv(plr_ent->get_pos()), GameConst::hsz_rat + 2,
			[&](auto& ent, auto&) {
				einter = &ent;
				++einter_num;
			},
			[](auto&, auto& fix) {
				auto f = getptr(&fix);
				return f && (f->typeflags & FixtureInfo::TYPEFLAG_INTERACTIVE);
			});
			
			if (einter_num == 1)
			{
				auto e = dynamic_cast<EInteractive*>(einter);
				if (!e) THROW_FMTSTR("Entity is not EInteractive - {}", einter->dbg_id());

				auto usage = e->use_string();
				
				std::string str;
				if (usage.first) str = FMT_FORMAT("[{}] ", pc_ctr->get_hint( PlayerController::A_INTERACT ));
				str += usage.second;
				
				RenImm::get().draw_text(
					RenderControl::get().get_world_camera().direct_cast(e->get_pos()),
					str, usage.first? 0xffffffff : 0xff6060ff, true
				);
				
				auto now = GameCore::get().get_step_time();
				if (pc_ctr->get_state().is[ PlayerController::A_INTERACT ] && now > interact_after)
				{
					e->use(plr_ent);
					interact_after = now + TimeSpan::seconds(0.5);
				}
			}
		}
		else {
			draw_text_hud({0, -1}, FMT_FORMAT("Rematerialization in {:.1f} seconds", plr_resp.seconds()));
		}
		
		// shouldn't be visible by player
		RenImm::get().set_context(RenImm::DEFCTX_WORLD);
		auto [ai_on, ai_off] = get_ai_rects();
		RenImm::get().draw_frame(ai_on,  0x00ff0060, 0.5);
		RenImm::get().draw_frame(ai_off, 0xff000060, 0.5);
		RenImm::get().set_context(RenImm::DEFCTX_UI);
	}
	void update_cheats() override
	{
		if (PlayerEntity::is_superman)
			cheat_godmode = true;

		if (cheat_godmode)
		{
			if (!plr_ent) plr_resp = {};
			else plr_ent->get_hlc()->add_filter(std::make_shared<DmgIDDQD>());
		}
		else
		{
			if (!plr_ent) return;
			
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
		{
			auto plr = static_cast<PlayerEntity*>(plr_ent);
			plr->eqp.infinite_ammo = cheat_ammo || cheat_godmode;
			plr->mov.cheat_infinite = cheat_godmode;
		}
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
	std::pair<Rectfp, Rectfp> get_ai_rects() override
	{
		if (fastforward) {
			auto& lc = LevelControl::get();
			Rectfp r {{}, vec2fp(lc.get_size()) * lc.cell_size, false};
			return {r, r};
		}
		
		// halfsizes
		const vec2fp hsz_on  = {80, 65};
		const vec2fp hsz_off = hsz_on * 1.25;
		
		if (auto ent = GameCore::get().get_ent(plr_eid))
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
			
			wpn_msgrep.str = {};
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
		update_cheats();
		
		plr_ent->eqp.msgrep = &wpn_msgrep;
	}
	void add_msg(std::string s)
	{
		msgs.emplace_back(std::move(s), TimeSpan::seconds(1.5), TimeSpan::seconds(1.5));
	}
};
PlayerManager* PlayerManager::create(std::shared_ptr<PlayerController> pc_ctr) {
	return new PlayerManager_Impl (std::move(pc_ctr));
}
