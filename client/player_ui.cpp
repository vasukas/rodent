#include <SDL2/SDL_endian.h>
#include "client/plr_input.hpp"
#include "client/sounds.hpp"
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
#include "render/texture.hpp"
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
			time = TimeSpan::current();
			switch(err)
			{
			case ERR_SELECT_OVERHEAT:
				str = "Overheated";
				break;
			case ERR_SELECT_NOAMMO:
				str = "No ammo";
				no_ammo_tcou.trigger();
				SoundEngine::once(SND_UI_NO_AMMO, {});
				break;
			case ERR_NO_TARGET:
				str = "No target";
				break;
			}
		}
		void no_ammo(int required) {
			time = TimeSpan::current();
			str = FMT_FORMAT("Need {} ammo", required);
			SoundEngine::once(SND_UI_NO_AMMO, {});
		}
		void reset() {
			str = {};
		}
	};
	
	TimeSpan wpn_ring_reload;
	TimeSpan wpn_ring_charge;
	SmoothBlink wpn_ring_lowammo;
	
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
		const float critical = 0.2; // warning threshold
		SmoothBlink blink;
		TimeSpan t_label;
		int ammo_prev = -1;
		bool is_cur = false;
		FColor last_flash_clr = {};
		
		void draw(Weapon& wpn, EC_Equipment& eqp, size_t index, bool& trigger_low_ammo)
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
					if (ammo_prev < ammo->value) {
						blink.trigger();
						last_flash_clr = FColor(1, 1, 0, a);
					}
					else if (ammo_prev > ammo->value && float(ammo->value) / ammo->max < critical) {
						blink.trigger();
						last_flash_clr = FColor(1, 1, 1, a);
						trigger_low_ammo = true;
					}
					ammo_prev = ammo->value;
					return (last_flash_clr * blink.get_blink()).to_px();
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
				
				RenImm::get().draw_rect(Rectfp::off_size(pos + p, z), clr1);
				p.x += z.x;
				z.x = (el_size.x - frame_width*2) - z.x;
				RenImm::get().draw_rect(Rectfp::off_size(pos + p, z), clr2);
			};
			std::vector<std::pair<FColor, std::string>> ss;
			
			// draw background
			
			RenImm::get().draw_rect(Rectfp::off_size(pos, el_size), bg_clr);
			
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
			
			RenImm::get().draw_frame(Rectfp::off_size(pos, el_size), frame_clr, frame_width);
			RenImm::get().draw_text(pos + el_off, std::to_string(index + 1));
			RenImm::get().draw_text(pos + el_off + vec2i(el_size.x, 0), std::move(ss));
			vig_space_line(el_y_space);
		}
		void draw(const PlayerNetworkHUD& wpnlist, size_t index, bool& trigger_low_ammo)
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
			
			auto& wpn = wpnlist.wpns[index];
			bool is_cur_now = (index == wpnlist.wpncur);
			if (is_cur_now && !is_cur) t_label = label_period;
			is_cur = is_cur_now;
			
			auto ri_model = static_cast<ModelType>(wpn.icon);
			EC_Equipment::Ammo* ammo;
			if (!wpn.ammomax) ammo = nullptr;
			else {
				static EC_Equipment::Ammo hack;
				hack.value = wpn.ammo;
				hack.max = wpn.ammomax;
				ammo = &hack;
			}
			
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
					if (ammo_prev < ammo->value) {
						blink.trigger();
						last_flash_clr = FColor(1, 1, 0, a);
					}
					else if (ammo_prev > ammo->value && float(ammo->value) / ammo->max < critical) {
						blink.trigger();
						last_flash_clr = FColor(1, 1, 1, a);
						trigger_low_ammo = true;
					}
					ammo_prev = ammo->value;
					return (last_flash_clr * blink.get_blink()).to_px();
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
				
				RenImm::get().draw_rect(Rectfp::off_size(pos + p, z), clr1);
				p.x += z.x;
				z.x = (el_size.x - frame_width*2) - z.x;
				RenImm::get().draw_rect(Rectfp::off_size(pos + p, z), clr2);
			};
			std::vector<std::pair<FColor, std::string>> ss;
			
			// draw background
			
			RenImm::get().draw_rect(Rectfp::off_size(pos, el_size), bg_clr);
			
			{	vec2fp img_size = ResBase::get().get_size(ri_model).size();
				float img_k = img_size.x / img_size.y;
				
				if (img_k > 1) img_size = {1, 1 / img_k};
				else           img_size = {img_k, 1};
				img_size *= el_size/2 - el_off;
			
				RenImm::get().draw_image(Rectfp::from_center( pos + el_size/2, img_size ),
				                         ResBase::get().get_image(ri_model));
			}
			
			// status
			
			if (t_label.is_positive())
			{
				float a = t_label / label_period;
				ss.emplace_back(FColor(1,1,1,a).to_px(), "PROXY");
				t_label -= RenderControl::get().get_passed();
			}
			ss.emplace_back(FColor{}, "\n");
			
			if (ammo)
			{
				if (!ammo->value) draw_strip(1, 0xff0000ff, 0, -1);
				else draw_strip(float(ammo->value)/ammo->max, 0xc0e0e0ff, 0, -1);
			}
			
			if (is_cur_now && wpnlist.wpn_overheat > 0)
			{
				if (!wpnlist.wpn_overheat_ok) {
					ss.emplace_back(FColor(1, 0.8, 0.5), "COOLDOWN");
					draw_strip(wpnlist.wpn_overheat, 0xff4040ff, 0, 0);
				}
				else {
					if (wpnlist.wpn_overheat > 0.5) ss.emplace_back(FColor(1, 1, 0), "Overheat");
					draw_strip(wpnlist.wpn_overheat, 0xc0c000ff, 0, 0);
				}
				ss.emplace_back(FColor{}, "\n");
			}
			if (is_cur_now && wpnlist.wpn_reload_show)
			{
				if (wpnlist.wpn_reload > 0)
					ss.emplace_back(FColor(0,1,0), "Reload");
			}
			if (is_cur_now && wpnlist.wpn_charge > 0)
			{
				ss.emplace_back(FColor(0.5, 1, 1), FMT_FORMAT("Charge: {:3}%", int_round(wpnlist.wpn_charge * 100)));
				draw_strip(wpnlist.wpn_charge, 0xc0c0ffff, 0xff000080, wpnlist.wpn_overheat > 0 ? 1 : 0);
			}
			
			//
			
			RenImm::get().draw_frame(Rectfp::off_size(pos, el_size), frame_clr, frame_width);
			RenImm::get().draw_text(pos + el_off, std::to_string(index + 1));
			RenImm::get().draw_text(pos + el_off + vec2i(el_size.x, 0), std::move(ss));
			vig_space_line(el_y_space);
		}
	};
	
	enum IndStat {
		INDST_HEALTH,
		INDST_ACCEL,
		INDST_ARMOR,
		INDST_PERS_SHIELD,
		INDST_PROJ_SHIELD,
		INDST_PROJ_SHIELD_REGEN,
		INDST_TOTAL_COUNT_INTERNAL
	};
	
	std::array<StatusIndicator, INDST_TOTAL_COUNT_INTERNAL> ind_stat;
	std::vector<WeaponIndicator> ind_wpns;
	bool proj_shld_was_dead = false;
	
	// interface - flare
	
	
	struct Flare {
		float flare_incr = 1.f / 0.5; // per second
		float flare_decr = 1.f / 1.2;
		FColor clr;
		bool is_big = false;
		
		float level = 0;
		std::optional<float> target;
		
		void trigger(float max = 1) {
			/*if (target) target = std::max(*target, max);
			else*/ target = max;
		}
		float update(float passed) {
			if (target) {
				if (level > target) target.reset();
				else level += flare_incr * passed;
			}
			else level = std::max(level - flare_decr * passed, 0.f);
			return level;
		}
		void reset() {
			level = 0;
			target.reset();
		}
	};
	
	enum FlareStat {
		FLARE_SHIELD,
		FLARE_HEALTH,
		FLARE_NO_SHIELD,
		FLARE_TOTAL_COUNT_INTERNAL
	};
	std::array<Flare, FLARE_TOTAL_COUNT_INTERNAL> flares;
	
	RAII_Guard flare_g;
	std::unique_ptr<Texture> flare_tex, flare_tex_big;
	
	
	
	PlayerUI_Impl()
	{
		ind_stat[INDST_HEALTH].clr = FColor(0x10c060ff);
		ind_stat[INDST_ACCEL].clr     = FColor(0xd07010ff);
		ind_stat[INDST_ACCEL].alt_clr = FColor(0x706060ff);
		//
		ind_stat[INDST_ARMOR].clr     = FColor(0x80a020ff);
		ind_stat[INDST_ARMOR].alt_clr = FColor(0x10c000ff);
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
		
		//
		
		flares[FLARE_SHIELD].clr = FColor(0.4, 0.9, 1);
		flares[FLARE_HEALTH].clr = FColor(0.4, 0, 0);
		flares[FLARE_HEALTH].flare_decr = 1.f / 3;
		flares[FLARE_HEALTH].is_big = true;
		flares[FLARE_NO_SHIELD].clr = FColor(0.4, 0.4, 0);
		flares[FLARE_NO_SHIELD].flare_decr = flares[FLARE_NO_SHIELD].flare_incr;
		
		flare_g = RenderControl::get().add_size_cb([this]
		{
			vec2i sz = RenderControl::get_size();
			std::vector<uint32_t> px (sz.area());
			
			// min offset, max offset, center min
			auto gen = [&](int x0, int x1, int y0)
			{
				float alpha_d = 255.f / x1;
				auto line = [&](int x, int y){
					int i; float a;
					for (a=0, i=x; i>=0; --i, a += alpha_d) {
						px[            y *sz.x + i] = px[            y *sz.x - i + sz.x - 1] =
						px[(sz.y - y - 1)*sz.x + i] = px[(sz.y - y - 1)*sz.x - i + sz.x - 1] =
							SDL_SwapBE32(0xffff'ff00 | int(a));
					}
				};
				for (int y=0; y<y0; ++y) {
					float t = float(y) / y0;
					line(lerp(x1, x0, t), y);
				}
				int y2 = sz.y/2;
				if (sz.y % 1) ++y2;;
				for (int y=y0; y<y2; ++y)
					line(x0, y);
			};
			
			gen(sz.x/6, sz.x/3, sz.y/5);
			flare_tex.reset(Texture::create_from( sz, Texture::FMT_RGBA, px.data() ));
			
			gen(sz.x/3, sz.x/2.5, sz.y/2.5);
			flare_tex_big.reset(Texture::create_from( sz, Texture::FMT_RGBA, px.data() ));
		});
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
			
			for (auto& f : flares)
				f.reset();
		}
		
		if (plr_ent)
		{
			if (mgr.cheat_godmode) vig_label("!!! GOD MODE ENABLED !!!\n");
			
			auto& hlc = plr_ent->hlc;
			auto& log = plr_ent->log;
			
			// flares
			if (AppSettings::get().plr_status_flare)
			{
				auto pst = log.shlc.get_state();
				switch (pst.first) {
					case ShieldControl::ST_DEAD:
					case ShieldControl::ST_DISABLED:
						break;
					case ShieldControl::ST_SWITCHING:
						flares[FLARE_SHIELD].trigger(0.3);
						break;
					case ShieldControl::ST_ACTIVE: {
							float t = log.shlc.get_ft()->get_hp().t_state();
							flares[FLARE_SHIELD].trigger(lerp(1, 0.5, t));
						}
						break;
				}
				
				if (hlc.get_hp().t_state() < 0.95) {
					if (flares[FLARE_HEALTH].level < 0.1) flares[FLARE_HEALTH].trigger();
					else flares[FLARE_HEALTH].trigger(1 - hlc.get_hp().t_state()/2);
				}
				else if (!log.pers_shld->get_hp().is_alive()) {
					flares[FLARE_NO_SHIELD].trigger(0.6);
				}
				
				FColor clr = {0,0,0,0};
				FColor clr_big = {0,0,0,0};
				for (auto& f : flares) {
					FColor c = f.clr;
					c.a = f.update(passed.seconds());
					(f.is_big ? clr_big : clr) += c * c.a;
				}
				
				RenImm::get().draw_image(Rectfp::bounds({}, flare_tex->get_size()), flare_tex_big.get(), clr_big.to_px());
				RenImm::get().draw_image(Rectfp::bounds({}, flare_tex->get_size()), flare_tex.get(), clr.to_px());
			}
			
			//
			
			// Health
			{	auto& hp = hlc.get_hp();
				ind_stat[INDST_HEALTH].draw(FMT_FORMAT("Health {:3}/{}", hp.exact().first, hp.exact().second), hp.t_state());
			}
			vig_lo_next();
			
			// Accelartion
			if (log.pmov.is_infinite_accel()) {
				ind_stat[INDST_ACCEL].draw("Accel INFINITE", 1, true);
			}
			else {
				auto acc_st = log.pmov.get_t_accel();
				ind_stat[INDST_ACCEL].draw(FMT_FORMAT("Accel {}", acc_st.first? "OK      " : "charge  "),
									       acc_st.second, !acc_st.first);
			}
			vig_lo_next();
			
			// Armor
			{	auto& hp = log.armor->get_hp();
				auto& ind = ind_stat[INDST_ARMOR];
				if (hp.t_state() > ind.prev_value) ind.blink_upd.trigger();
				ind.draw(FMT_FORMAT("Armor  {:3}/{}", hp.exact().first, hp.exact().second),
				         hp.t_state(), hp.t_state() >= log.armor->maxmod_t);
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
							SoundEngine::once(SND_UI_SHIELD_READY, {});
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
				
				bool trigger_low_ammo = false;
				for (size_t i=0; i < wpns.size(); ++i)
					ind_wpns[i].draw( *wpns[i], eqp, i, trigger_low_ammo );
				
				if (trigger_low_ammo)
					wpn_ring_lowammo.trigger();
			}
			
			const TimeSpan wmr_max = TimeSpan::seconds(1.5);
			TimeSpan wmr_passed = TimeSpan::current() - wpn_msgrep.time;
			if (wmr_passed < wmr_max)
			{
				uint32_t clr = 0xffffff00 | lerp(0, 255, 1 - wmr_passed / wmr_max);
				RenImm::get().draw_text(vec2fp(mou_pos.x, mou_pos.y + 30), wpn_msgrep.str, clr, true);
			}
			if (!eqp.has_ammo( eqp.get_wpn() )) {
				RenImm::get().draw_text(vec2fp(mou_pos.x, mou_pos.y - 30), "Ammo: 0", 0xffff'ffc0, true);
			}
			if (float t = wpn_ring_lowammo.get_blink(); t > 0.01) {
				int left = eqp.get_ammo(eqp.get_wpn().info->ammo).value;
				RenImm::get().draw_text(vec2fp(mou_pos.x, mou_pos.y + 30), FMT_FORMAT("LOW AMMO ({})", left), 0xffff'ffc0, true);
			}
			
			// Cursor
			
//			int cursor_rings_x = 0;
			if (AppSettings::get().cursor_info_flags & 1)
			{
				const float base_radius = 20;
				float radius = base_radius;
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
					float t = fracpart(now.seconds());
					if (t > 0.5) t = 1 - t;
					t *= 2;
					
					uint32_t clr = 0xff000000 | int(alpha * ammo_t);
					clr += lerp<int>(64, 220, t) << 16;
					clr += lerp<int>(64, 220, t) << 8;
					
					RenImm::get().mouse_cursor_hack();
					draw_progress_ring({}, 1, clr, radius, width);
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
						RenImm::get().mouse_cursor_hack();
						draw_progress_ring({}, t, clr, radius, width);
						radius += width;
					}
					if (auto& m = wpn.overheat)
					{
						uint32_t clr = m->flag ? 0xff606000 : 0xffff0000;
						RenImm::get().mouse_cursor_hack();
						draw_progress_ring({}, m->value, clr | alpha, radius, width);
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
							RenImm::get().mouse_cursor_hack();
							draw_progress_ring({}, *m->charge_t, clr, radius, width);
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
			
			if (full_info)
			{
				stat_str += FMT_FORMAT("Looking at: {}\n", dstate.lookat);
				
				auto& lc = plr_ent->core.get_lc();
				auto room = lc.get_room(plr_ent->get_pos());
				stat_str += FMT_FORMAT("Room: {}\n", room? room->name : "Corridor");
				
				if (auto c = lc.cell( lc.to_cell_coord( pinp.get_state(PlayerInput::CTX_GAME).tar_pos ) ))
					stat_str += FMT_FORMAT("{} {}x{}\n", c->is_wall ? "Wall" : "Cell", c->pos.x, c->pos.y);
			}
			stat_str += FMT_FORMAT("Objective: {}", objective);
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
		if (debug_mode) {
			RenImm::get().set_context(RenImm::DEFCTX_WORLD);
			auto [ai_on, ai_off] = mgr.get_ai_rects();
			RenImm::get().draw_frame(ai_on,  0x00ff0060, 0.5);
			RenImm::get().draw_frame(ai_off, 0xff000060, 0.5);
			RenImm::get().set_context(RenImm::DEFCTX_UI);
		}
	}
	void render(const PlayerNetworkHUD& dst, bool dst_exists, TimeSpan passed, vec2i mou_pos)
	{
		if (!dst_exists)
		{
			for (auto& i : ind_stat) {
				i.blink.force_reset();
				i.blink_upd.force_reset();
			}
			ind_wpns.clear();
			
			for (auto& f : flares)
				f.reset();
		}
		
		if (dst_exists)
		{
			// flares
			if (AppSettings::get().plr_status_flare)
			{
				auto pst_first = static_cast<ShieldControl::State>(dst.shlc_state);
				switch (pst_first) {
					case ShieldControl::ST_DEAD:
					case ShieldControl::ST_DISABLED:
						break;
					case ShieldControl::ST_SWITCHING:
						flares[FLARE_SHIELD].trigger(0.3);
						break;
					case ShieldControl::ST_ACTIVE: {
							float t = dst.shlc_t;
							flares[FLARE_SHIELD].trigger(lerp(1, 0.5, t));
						}
						break;
				}
				
				if (dst.hlc_t < 0.95) {
					if (flares[FLARE_HEALTH].level < 0.1) flares[FLARE_HEALTH].trigger();
					else flares[FLARE_HEALTH].trigger(1 - dst.hlc_t/2);
				}
				else if (!dst.pers_shld_alive) {
					flares[FLARE_NO_SHIELD].trigger(0.6);
				}
				
				FColor clr = {0,0,0,0};
				FColor clr_big = {0,0,0,0};
				for (auto& f : flares) {
					FColor c = f.clr;
					c.a = f.update(passed.seconds());
					(f.is_big ? clr_big : clr) += c * c.a;
				}
				
				RenImm::get().draw_image(Rectfp::bounds({}, flare_tex->get_size()), flare_tex_big.get(), clr_big.to_px());
				RenImm::get().draw_image(Rectfp::bounds({}, flare_tex->get_size()), flare_tex.get(), clr.to_px());
			}
			
			//
			
			// Health
			{
				ind_stat[INDST_HEALTH].draw(FMT_FORMAT("Health {:3}/{}", dst.hlc_hp, dst.hlc_hpmax), dst.hlc_t);
			}
			vig_lo_next();
			
			// Accelartion
			if (dst.t_accel < 0) {
				ind_stat[INDST_ACCEL].draw("Accel INFINITE", 1, true);
			}
			else {
				ind_stat[INDST_ACCEL].draw(FMT_FORMAT("Accel {}", dst.t_accel_enabled? "OK      " : "charge  "),
									       dst.t_accel, !dst.t_accel_enabled);
			}
			vig_lo_next();
			
			// Armor
			{
				auto& ind = ind_stat[INDST_ARMOR];
				if (dst.arm_t > ind.prev_value) ind.blink_upd.trigger();
				ind.draw(FMT_FORMAT("Armor  {:3}/{}", dst.arm_hp, dst.arm_hpmax),
				         dst.arm_t, dst.arm_atmax);
			}
			vig_lo_next();
			
			// Shield
			{
				ind_stat[INDST_PERS_SHIELD].draw(FMT_FORMAT("P.shld {:3}/{}", dst.pers_hp, dst.pers_hpmax), dst.pers_t);
			}
			vig_lo_next();
			
			// Projected shield
			{
				auto& i_alive = ind_stat[INDST_PROJ_SHIELD];
				auto& i_regen = ind_stat[INDST_PROJ_SHIELD_REGEN];
				
				switch (dst.shlc_state)
				{
				case ShieldControl::ST_DEAD: {
						proj_shld_was_dead = true;
						i_regen.draw("Regenerating...", dst.shlc_time / ShieldControl::dead_time);
					}
					break;
					
				case ShieldControl::ST_DISABLED: {
						if (std::exchange(proj_shld_was_dead, false)) {
							i_alive.blink.force_reset();
							i_alive.blink.trigger();
							SoundEngine::once(SND_UI_SHIELD_READY, {});
						}
						i_alive.draw("Shield - ready", 0, true);
					}
					break;
					
				case ShieldControl::ST_SWITCHING: {
						i_regen.draw("Shield        ", 1, true);
					}
					break;
					
				case ShieldControl::ST_ACTIVE: {
						i_alive.draw(FMT_FORMAT("Shield {:3}/{}", dst.shlc_hp, dst.shlc_hpmax), dst.shlc_t);
					}
					break;
				}
			}
			vig_lo_next();
			
			auto& cur_wpn = dst.wpns[dst.wpncur];
			{
				vig_label_a("{}\nAmmo: {} / {}\n", "PROXY", cur_wpn.ammo, cur_wpn.ammomax);
			}
			{
				ind_wpns.resize( dst.wpns.size() );
				
				bool trigger_low_ammo = false;
				for (size_t i=0; i < dst.wpns.size(); ++i)
					ind_wpns[i].draw( dst, i, trigger_low_ammo );
				
				if (trigger_low_ammo)
					wpn_ring_lowammo.trigger();
			}
			
			const TimeSpan wmr_max = TimeSpan::seconds(1.5);
			TimeSpan wmr_passed = TimeSpan::current() - wpn_msgrep.time;
			if (wmr_passed < wmr_max)
			{
				uint32_t clr = 0xffffff00 | lerp(0, 255, 1 - wmr_passed / wmr_max);
				RenImm::get().draw_text(vec2fp(mou_pos.x, mou_pos.y + 30), wpn_msgrep.str, clr, true);
			}
			if (!cur_wpn.ammo) {
				RenImm::get().draw_text(vec2fp(mou_pos.x, mou_pos.y - 30), "Ammo: 0", 0xffff'ffc0, true);
			}
			if (float t = wpn_ring_lowammo.get_blink(); t > 0.01) {
				int left = cur_wpn.ammo;
				RenImm::get().draw_text(vec2fp(mou_pos.x, mou_pos.y + 30), FMT_FORMAT("LOW AMMO ({})", left), 0xffff'ffc0, true);
			}
			
			// Cursor
			
//			int cursor_rings_x = 0;
			if (AppSettings::get().cursor_info_flags & 1)
			{
				const float base_radius = 20;
				float radius = base_radius;
				const float width = 6;
				const int alpha = 0xc0;
				
				const TimeSpan hide = TimeSpan::seconds(2);
				const TimeSpan fade = TimeSpan::seconds(0.4);
				TimeSpan now = TimeSpan::since_start();
				
				//
				
				auto& ammo_tcou = wpn_msgrep.no_ammo_tcou;
				if (!cur_wpn.ammo) ammo_tcou.trigger();
				float ammo_t = ammo_tcou.get_blink();
				
				if (ammo_t > 0.01)
				{
					float t = fracpart(now.seconds());
					if (t > 0.5) t = 1 - t;
					t *= 2;
					
					uint32_t clr = 0xff000000 | int(alpha * ammo_t);
					clr += lerp<int>(64, 220, t) << 16;
					clr += lerp<int>(64, 220, t) << 8;
					
					RenImm::get().mouse_cursor_hack();
					draw_progress_ring({}, 1, clr, radius, width);
					radius += width;
				}
				else
				{
					if (dst.wpn_reload_show)
					{
						uint32_t clr = alpha; float t;
						if (dst.wpn_reload > 0) {
							clr |= 0xc0ff4000;
							t = 1 - dst.wpn_reload;
							wpn_ring_reload = now;
						}
						else {
							auto diff = now - wpn_ring_reload;
							if (diff > hide) clr = alpha * clampf_n(1. - (diff - hide) / fade);
							clr |= 0x40ff4000;
							t = 1;
						}
						RenImm::get().mouse_cursor_hack();
						draw_progress_ring({}, t, clr, radius, width);
						radius += width;
					}
					if (dst.wpn_overheat > 0)
					{
						uint32_t clr = !dst.wpn_overheat_ok ? 0xff606000 : 0xffff0000;
						RenImm::get().mouse_cursor_hack();
						draw_progress_ring({}, dst.wpn_overheat, clr | alpha, radius, width);
						radius += width;
					}
					if (dst.wpn_charge > 0)
					{
						if (true)
						{
							uint32_t clr = alpha;
							if (dst.wpn_charge > 0.99) {
								auto diff = now - wpn_ring_charge;
								if (diff > hide) clr = alpha * clampf_n(1. - (diff - hide) / fade);
								clr |= 0xe0ffff00;
							}
							else {
								clr |= 0x40c0ff00;
								wpn_ring_charge = now;
							}
							RenImm::get().mouse_cursor_hack();
							draw_progress_ring({}, dst.wpn_charge, clr, radius, width);
						}
						radius += width;
					}
				}
//				cursor_rings_x = radius;
			}
		}
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
