#include <SDL2/SDL_events.h>
#include "game_ui.hpp"

#include "client/ec_render.hpp"
#include "client/level_map.hpp"
#include "client/player_ui.hpp"
#include "client/plr_input.hpp"
#include "client/presenter.hpp"
#include "client/replay.hpp"
#include "core/hard_paths.hpp"
#include "core/main_loop.hpp"
#include "core/vig.hpp"
#include "game/game_core.hpp"
#include "game/game_mode.hpp"
#include "game/player_mgr.hpp"
#include "render/camera.hpp"
#include "render/control.hpp"
#include "render/ren_aal.hpp"
#include "render/ren_imm.hpp"
#include "render/texture.hpp"
#include "utils/time_utils.hpp"
#include "vaslib/vas_log.hpp"

#include "core/settings.hpp"
#include "game/damage.hpp"
#include "game/game_info_list.hpp"
#include "game/physics.hpp"
#include "game_ai/ai_drone.hpp"
#include "game_objects/objs_basic.hpp"
#include "game_objects/weapon_all.hpp"
#include "render/postproc.hpp"
#include "render/ren_text.hpp"
#include "utils/path_search.hpp"
#include "utils/res_image.hpp"



struct WinrarAnim
{
	static const int off = 100;
	vec2fp pos, spd;
	FColor clr;
	bool is_win;
	
	WinrarAnim(bool is_win): is_win(is_win) {gen();}
	void draw()
	{
		float t = RenderControl::get().get_passed().seconds();

		TextureReg tx = ResBase::get().get_image(MODEL_WINRAR);
		Rectfp dst = Rectfp::from_center(pos, tx.px_size());
		RenImm::get().draw_image(dst, tx, clr.to_px());
		
		spd.y += 70 * t;
		pos += spd * t;
		
		if (pos.y > RenderControl::get_size().y + off)
			gen();
	}
	void gen()
	{
		if (is_win)
		{
			pos.x = rnd_stat().range(-off, RenderControl::get_size().x + off);
			pos.y = RenderControl::get_size().y + off;
			
			spd.x = rnd_stat().range_n2() * 70;
			spd.y = -rnd_stat().range(150, 400);
		}
		else
		{
			pos.x = rnd_stat().range(-off, RenderControl::get_size().x + off);
			pos.y = -off;
			
			spd.x = rnd_stat().range_n2() * 15;
			spd.y = rnd_stat().range(20, 300);
		}
		
		clr.r = rnd_stat().range(0.5, 1);
		clr.g = rnd_stat().range(0.6, 1);
		clr.b = rnd_stat().range(0.7, 1);
		clr.a = 1;
	}
};



struct CameraControl
{
	bool is_close_cam = false;
	bool is_far_cam = false;
	TimeSpan cam_telep_tmo;
	
	CameraControl()
	{
		auto& cam = RenderControl::get().get_world_camera();
		cam.mut_state().pos = vec2fp::one(-1000);
	}
	void update(vec2fp target, TimeSpan passed)
	{
		auto& cam = RenderControl::get().get_world_camera();
		const float per_second = 0.08 / TimeSpan::fps(60).seconds();
		
		vec2fp cam_size_m;
		if (is_close_cam) cam_size_m = {60, 35};
		else cam_size_m = {60, 50};
		float calc_mag = (vec2fp(RenderControl::get().get_size()) / cam_size_m).minmax().x;
		
		auto frm = cam.get_state();
		if (is_far_cam) calc_mag /= 1.5;
		frm.mag = calc_mag;
		
		const vec2fp scr = cam.coord_size();
		const vec2fp tar_d = (target - frm.pos);
		
		if (std::fabs(tar_d.x) > scr.x || std::fabs(tar_d.y) > scr.y)
		{
			auto half = TimeSpan::seconds(0.5);
			if (!cam_telep_tmo.is_positive())
			{
				Postproc::get().tint_reset();
				Postproc::get().tint_seq(half, FColor(0,0,0,0));
				Postproc::get().tint_default(half);
			}
			
			cam_telep_tmo += passed;
			if (cam_telep_tmo > half)
			{
				cam_telep_tmo = {};
				frm.pos = target;
				cam.set_state(frm);
			}
			else cam.set_state(frm);
		}
		else
		{
			vec2fp dt = (target - frm.pos) * (per_second * passed.seconds());
			
			cam_telep_tmo = {};
			frm.pos += dt;
			cam.set_state(frm);
		}
	}
};



class GameUI_Impl : public GameUI
{
public:
	// main
	
	GameControl& gctr;
	bool gctr_inited = false;
	
	std::optional<float> replay_speed_k;
	bool is_playback = false;
	
	// screen
	
	bool ignore_ren_pause = false;
	
	bool is_ren_paused = false; ///< Is paused be window becoming non-visible
	TimeSpan hide_pause_until; ///< Pause stub is not drawn until after that time
	bool is_first_frame = true;
	
	std::shared_ptr<LevelTerrain> level_terrain;
	
	// interface
	
	CameraControl cctr;
	
	std::unique_ptr<LevelMap> lmap; ///< May be null
	std::unique_ptr<Texture> crosshair_tex;
	
	std::vector<WinrarAnim> winrars;
	std::string init_greet;
	
	PlayerUI* pmg_ui;
	bool menu_pause = false;
	
	TimeSpan since_victory;
	
	SmoothBlink boot_blink;
	float prev_boot_level = 2; // >1, to flash on appear
	
	bool is_tutorial = false;
	
	// debug controls
	
	bool allow_cheats = false;
	bool ph_debug_draw = false;
	
	bool ui_dbg_mode = false;
	EntityIndex dbg_select;
	bool ui_dbg_puppet_switch = false;
	
	bool free_camera_enabled = false;
	vec2fp free_camera_shift = {};
	bool dbg_paused = false;
	
	// debug stats
	
	RAII_Guard dbg_serv_g, dbg_rend_g;
	std::optional<vigAverage> dbg_serv_avg;
	
	double serv_avg_total = 0;
	size_t serv_avg_count = 0;
	size_t serv_overmax_count = 0;
	
	struct DebugAPS {
		float time;
		size_t reqs;
	};
	std::array<DebugAPS, int(TimeSpan::seconds(5) / GameCore::step_len)> dbg_aps = {};
	size_t i_dbg_aps = 0;
	
	
	
	GameUI_Impl(InitParams pars)
		: gctr(*pars.ctr)
	{
		PlayerInput::get().set_context(PlayerInput::CTX_GAME);
		init_greet = std::move(pars.init_greet);
		allow_cheats = pars.allow_cheats;
		is_tutorial = pars.is_tutorial;
		
		crosshair_tex.reset(Texture::load(HARDPATH_CROSSHAIR_IMG));
		
		if (pars.debug_menu)
		{
			dbg_rend_g = vig_reg_menu(VigMenu::DebugRenderer, [this]
			{
				auto& t = AppSettings::get_mut().aal_type;
				auto bt = [&](auto type, auto text){
					if (vig_button(text, 0, t == type)) {
						t = type;
						RenAAL::get().reinit_glow();
						GamePresenter::get()->reinit_resources(*level_terrain);
					}
				};
				vig_label("AAL glow");
				bt(AppSettings::AAL_OldFuzzy, "OldFuzzy");
				bt(AppSettings::AAL_CrispGlow, "CrispGlow");
				bt(AppSettings::AAL_Clear, "Clear");
				vig_lo_next();
			});
			
			dbg_serv_avg.emplace(5.f, GameCore::time_mul);
			
			dbg_serv_g = vig_reg_menu(VigMenu::DebugGame, [this]
			{
				auto lock = gctr.core_lock();
				auto& core = gctr.get_core();
				
				dbg_serv_avg->draw();
				vig_lo_next();
				
				if (vig_button("Save world render")) {
					lock.unlock();
					save_automap("AUTOMAP.png");
					return;
				}
				vig_checkbox(pmg_ui->full_info, "Additional HUD info");
				vig_checkbox(ignore_ren_pause, "Ignore render pause");
				vig_checkbox(core.get_aic().show_aos_debug, "Show AOS");
				vig_checkbox(core.get_aic().show_states_debug, "See AI stats");
				vig_checkbox(RenParticles::get().enabled, "Show particles");
				vig_lo_next();
				
				vig_label_a("Raycasts:  {:4}\nAABB query: {:3}\n",
				            core.get_phy().raycast_count, core.get_phy().aabb_query_count);
				vig_label_a("Bots (battle): {}\n", core.get_aic().debug_batle_number);
				vig_lo_next();
				
				//
				
				float ad_time = 0;
				size_t ad_reqs = 0;
				size_t ad_locks = 0;
				for (auto& p : dbg_aps) {
					ad_time  = std::max(ad_time,  p.time);
					ad_reqs  = std::max(ad_reqs,  p.reqs);
				}
				
				vig_label_a("Time: {:2.3f}\nReqs:  {:3}\nLocks: {:3}\n",
				            ad_time, ad_reqs, ad_locks);
				vig_lo_next();
				
				static uint32_t last_step = 0;
				if (core.get_step_counter() != last_step)
				{
					auto& aps = core.get_lc().get_aps();
					
					dbg_aps[i_dbg_aps].time = aps.debug_time.seconds();
					dbg_aps[i_dbg_aps].reqs = aps.debug_request_count;
					i_dbg_aps = (i_dbg_aps + 1) % dbg_aps.size();
					
					last_step = core.get_step_counter();
					aps.debug_time = {};
					aps.debug_request_count = {};
				}
				
				//
				
				if (allow_cheats)
				{
					vig_checkbox(core.dbg_ai_attack, "AI attack");
					vig_checkbox(core.dbg_ai_see_plr, "AI see player");
					vig_lo_next();
				}
				if (auto ent = core.get_pmg().get_ent())
				{
					auto pos = ent->get_pos();
					vig_label_a("x:{:5.2f} y:{:5.2f}", pos.x, pos.y);
					vig_lo_next();
					
					if (vig_button("Damage self (30)"))
						ent->ref_hlc().apply({ DamageType::Direct, 30 });
					
					if (allow_cheats)
					{
						bool upd_cheats = false;
						upd_cheats |= vig_checkbox(core.get_pmg().cheat_ammo, "Infinite ammo");
						upd_cheats |= vig_checkbox(core.get_pmg().cheat_godmode, "God mode");
						upd_cheats |= vig_checkbox(core.get_pmg().is_superman, "Superman (requires respawn)");
						if (upd_cheats) core.get_pmg().update_cheats();
						vig_lo_next();
						
						if (vig_button("Get all keys")) {
							for (int i=0; i<3; ++i)
								core.get_gmc().inc_objective();
						}
						if (vig_button("Get 500 armor")) {
							new EPickable(core, pos, EPickable::ArmorShard{500});
						}
						if (vig_button("Get ALL ammo")) {
							for (int i=0; i<int(AmmoType::TOTAL_COUNT); ++i)
								new EPickable(core, pos, EPickable::AmmoPack{AmmoType(i), 100});
						}
						vig_checkbox(ent->ref_eqp().no_overheat, "No overheat");
						vig_lo_next();
						
						vig_label("WEAPONS! (replaces 6)\n");
						Weapon* new_wpn = nullptr;
						if (vig_button("Normal"))  new_wpn = new WpnUber;
						if (vig_button("Turret"))  new_wpn = new WpnMinigunTurret;
						if (vig_button("SMG"))     new_wpn = new WpnSMG;
						if (vig_button("Barrage")) new_wpn = new WpnBarrage;
						if (vig_button("E Oneshot")) new_wpn = new WpnElectro(WpnElectro::T_ONESHOT);
						if (vig_button("E Worker"))  new_wpn = new WpnElectro(WpnElectro::T_WORKER);
						if (vig_button("E Camper"))  new_wpn = new WpnElectro(WpnElectro::T_CAMPER);
						if (new_wpn) ent->ref_eqp().replace_wpn(5, std::unique_ptr<Weapon>(new_wpn));
					}
					else {
						vig_lo_next();
						vig_label("Cheats disabled");
					}
					vig_lo_next();
				}
			});
		}
	}
	~GameUI_Impl()
	{
		gctr.set_post_step({});
		VLOGI("Average logic frame length: {} ms, {} samples", serv_avg_total, serv_avg_count);
		VLOGI("Logic frame length > sleep time: {} samples", serv_overmax_count);
	}
	void init()
	{
		std::shared_ptr<LevelTerrain> lt;
		{
			auto lock = gctr.core_lock();
			lt = gctr.get_terrain();
			
			gctr.set_post_step([this](TimeSpan dt)
			{
				if (dt > GameCore::step_len) {
					VLOGD("Server lag: {:.3f} seconds on step {}", dt.seconds(), serv_avg_count);
					++serv_overmax_count;
				}
				
				if (dbg_serv_avg)
					dbg_serv_avg->add(dt.seconds());
				
				++serv_avg_count;
				serv_avg_total += (dt.seconds() * 1000 - serv_avg_total) / serv_avg_count;
			});
			
			is_playback = gctr.get_replay_reader();
			pmg_ui = PlayerUI::create();
			pmg_ui->debug_mode = allow_cheats;
			gctr.get_core().get_pmg().set_pui( std::unique_ptr<PlayerUI>(pmg_ui) );
		}
		
		RenAAL::get().draw_grid = true;
		Postproc::get().tint_seq({}, FColor(0,0,0,0));
		
		if (lt) {
			lmap.reset( LevelMap::init(*lt) );
			level_terrain = std::move(lt);
		}
	}
	void on_leave()
	{
		RenAAL::get().draw_grid = false;
		RenParticles::get().enabled = false;
		
		auto lock = gctr.core_lock();
		gctr.set_pause(true);
	}
	void on_enter()
	{
		RenAAL::get().draw_grid = true;
		RenParticles::get().enabled = true;
		hide_pause_until = TimeSpan::since_start() + TimeSpan::seconds(0.5);
		
		auto lock = gctr.core_lock();
		gctr.set_pause(false);
		
//		Postproc::get().tint_reset();
//		Postproc::get().tint_seq({}, FColor(0.5, 0.5, 0.5, 0.5));
//		Postproc::get().tint_default(TimeSpan::seconds(0.2));
	}
	void on_event(const SDL_Event& ev)
	{
		auto mod_pb_speed = [&](auto f)
		{
			if (!is_playback) return;
			float v = f(replay_speed_k.value_or(1));
			if (aequ(v, 1, 0.01)) replay_speed_k = {};
			else replay_speed_k = v;
		};
		
		if (ev.type == SDL_KEYDOWN)
		{
			int k = ev.key.keysym.scancode;
			if (!is_first_frame && allow_cheats)
			{
				if (k == SDL_SCANCODE_LSHIFT || k == SDL_SCANCODE_RSHIFT) {
					ui_dbg_mode = true;
					ui_dbg_puppet_switch = false;
				}
			}
		}
		else if (ev.type == SDL_KEYUP)
		{
			int k = ev.key.keysym.scancode;
			
			if (is_ren_paused) {
				is_ren_paused = false;
				return;
			}
			
			if		(k == SDL_SCANCODE_1) mod_pb_speed([](float v){return v/2;});
			else if (k == SDL_SCANCODE_2) mod_pb_speed([](float v){return v*2;});
			else if (k == SDL_SCANCODE_3) mod_pb_speed([](float){return 1;});
			else if (k == SDL_SCANCODE_PAUSE) is_ren_paused = true;
			else if (!is_first_frame && allow_cheats)
			{
				if		(k == SDL_SCANCODE_0) {
					ph_debug_draw = !ph_debug_draw;
				}
				else if (k == SDL_SCANCODE_B) {
					cctr.is_far_cam = !cctr.is_far_cam;
				}
				else if (k == SDL_SCANCODE_F4)
				{
					auto lock = gctr.core_lock();
					auto& pm = gctr.get_core().get_pmg();
					pm.cheat_godmode = !pm.cheat_godmode;
					pm.update_cheats();
				}
				else if (k == SDL_SCANCODE_F7) {
					ui_dbg_mode = !ui_dbg_mode;
					ui_dbg_puppet_switch = false;
				}
				else if (k == SDL_SCANCODE_LSHIFT || k == SDL_SCANCODE_RSHIFT) {
					ui_dbg_mode = false;
				}
				else if (k == SDL_SCANCODE_H) {
					dbg_paused = !dbg_paused;
					auto lock = gctr.core_lock();
					gctr.set_pause(dbg_paused);
				}
				else if (k == SDL_SCANCODE_J) {
					auto lock = gctr.core_lock();
					gctr.step_paused(1);
				}
				else if (k == SDL_SCANCODE_K) {
					free_camera_enabled = !free_camera_enabled;
				}
				else if (k == SDL_SCANCODE_L) {
					free_camera_shift = {};
				}
				else if (ui_dbg_mode)
				{
					if (k == SDL_SCANCODE_P)
						ui_dbg_puppet_switch = !ui_dbg_puppet_switch;
				}
			}
		}
		else if (ev.type == SDL_MOUSEBUTTONUP)
		{
			is_ren_paused = false;
		}
		
		if (!ui_dbg_mode && !is_ren_paused) {
			auto g = PlayerInput::get().lock();
			PlayerInput::get().on_event(ev);
		}
	}
	void render(TimeSpan frame_time, TimeSpan passed)
	{
		auto gstate = gctr.get_state();
		if (!gctr_inited && !std::holds_alternative<GameControl::CS_Init>(gstate)) {
			gctr_inited = true;
			init();
		}
		
		if (auto st = std::get_if<GameControl::CS_Init>(&gstate))
		{
			draw_text_message(st->stage + "\n\n" + init_greet);
		}
		else if (auto st = std::get_if<GameControl::CS_End>(&gstate))
		{
			if (st->is_error) {
				LOG_THROW("Game exception - {}", st->message);
				// exits
			}
			if (!st->message.empty())
			{
				draw_text_message(st->message);
				return;
			}
			
			RenImm::get().set_context(RenImm::DEFCTX_WORLD);
			GamePresenter::get()->render(frame_time, passed);
			RenImm::get().set_context(RenImm::DEFCTX_UI);
			
			if (st->won == GameModeCtr::State::TutComplete) {
				if (!since_victory.is_positive()) {
					Postproc::get().tint_reset();
					Postproc::get().tint_seq(TimeSpan::seconds(3), FColor(0,0,0));
				}
				draw_text_message("Training session completed");
			}
			else {
				RenImm::get().draw_rect({{}, RenderControl::get_size(), false}, 0xa0);
				
				bool is_win = (st->won == GameModeCtr::State::Won);
				
				if (winrars.empty()) {
					for (int i=0; i<40; ++i)
						winrars.emplace_back(is_win);
				}
				for (auto& w : winrars) w.draw();
				
				if (is_win)
					draw_text_message("Game completed.\n\nA WINRAR IS YOU.");
				else
					draw_text_message("Terminal destroyed.\n\nYOU LOST.");
			}
			
			since_victory += passed;
			if (since_victory > TimeSpan::seconds(2)) {
				draw_text_hud(vec2i{RenderControl::get_size().x /2, -1}, "Press ESCAPE to EXIT", -1, true, 2);
			}
			
			auto& pinp = PlayerInput::get();
			pinp.set_context(PlayerInput::CTX_MENU);
			pinp.update(PlayerInput::CTX_MENU);
			if (pinp.get_state(PlayerInput::CTX_MENU).is[PlayerInput::A_MENU_EXIT]) {
				delete MainLoop::current;
				return;
			}
		}
		else if (!RenderControl::get().is_visible() && !ignore_ren_pause)
		{
			auto lock = gctr.core_lock();
			gctr.set_pause(true);
			is_ren_paused = true;
		}
		else
		{
			if (SDL_GetKeyboardFocus() != RenderControl::get().get_wnd() && !ignore_ren_pause)
				is_ren_paused = true;
			
			auto lock = gctr.core_lock();
			auto& core = gctr.get_core();
			
//			bool is_core_paused = std::get<GameControl::CS_Run>(gctr.get_state()).paused;
			gctr.set_pause(is_ren_paused || menu_pause || dbg_paused);
			gctr.set_speed(replay_speed_k);
			
			if (is_first_frame)
			{
				VLOGI("First game frame rendered at {:.3f} seconds", TimeSpan::since_start().seconds());
				is_first_frame = false;
			}
			
			// input
			
			auto& pinp = PlayerInput::get();
			pinp.update(PlayerInput::CTX_MENU);
			auto& inpst = pinp.get_state(PlayerInput::CTX_GAME);
			
			// set camera
			
			if (auto ent = core.get_pmg().get_ent())
			{
				const float tar_min = GameConst::hsz_rat * 2;
				const float tar_max = 15.f;
//				const float tar_max = 15.f * (calc_mag / cam.get_state().mag);
				
				const vec2fp pos = GamePresenter::get()->playback_hack
				                   ? ent->get_pos()
				                   : ent->ref<EC_RenderPos>().get_cur().pos;
				vec2fp tar = pos;
				if (inpst.is[PlayerInput::A_CAM_FOLLOW])
				{
					tar = inpst.tar_pos;
					
					vec2fp tar_d = (tar - pos);
					if		(tar_d.len() < tar_min) tar = pos;
					else if (tar_d.len() > tar_max) tar = pos + tar_d.get_norm() * tar_max;
				}
				if (free_camera_enabled)
				{
					free_camera_shift += inpst.mov * 50 * passed.seconds();
					tar += free_camera_shift;
				}
				
				cctr.is_close_cam = inpst.is[PlayerInput::A_CAM_CLOSE_SW];
				cctr.update(tar, passed);
			}
			
			// draw world
			
			RenImm::get().set_context(RenImm::DEFCTX_WORLD);
			GamePresenter::get()->render( frame_time, passed );
			
			if (ph_debug_draw)
				core.get_phy().world.DebugDraw();
			
			// draw pause stub
			
			if (is_ren_paused)
			{
				if (TimeSpan::since_start() > hide_pause_until)
				{
					RenImm::get().set_context(RenImm::DEFCTX_UI);
					RenImm::get().draw_rect({{}, RenderControl::get_size(), false}, 0xa0);
					draw_text_message("PAUSED\nPress any key to continue");
				}
				return;
			}
			
			// draw UI
			
			RenImm::get().set_context(RenImm::DEFCTX_UI);
			
			// draw HUD
			
			bool use_crosshair = is_playback;
			vec2i g_mpos = inpst.cursor;
			
			if (vig_current_menu() == VigMenu::Default && !core.get_info().get_menu_teleport())
			{
				core.get_pmg().render(passed, g_mpos);
				
				if (use_crosshair) // draw
				{
					float z = std::max(10., RenderControl::get_size().minmax().y * 0.03);
					uint32_t clr = 0x00ff20c0;
					
					vec2fp p = inpst.tar_pos;
					p = RenderControl::get().get_world_camera().direct_cast(p);
					RenImm::get().draw_image(Rectfp::from_center(p, vec2fp::one(z/2)), crosshair_tex.get(), clr);
				}
			}
			
			// draw objective state
			
			if (core.get_gmc().get_state() == GameModeCtr::State::Booting)
			{
				float lvl = core.get_gmc().get_boot_level();
				float t_lvl = boot_blink.get_sine(lvl < prev_boot_level);
				prev_boot_level = lvl;
				
				Rectfp at = {};
				float lht = RenText::get().line_height(FontIndex::Mono);
				at.a.set(RenderControl::get_size().x/4, 0);
				at.b.set(RenderControl::get_size().x - RenderControl::get_size().x/4, lht + 4);
				vec2fp ctr = at.center();
				
				FColor clr = FColor(1, 0.2, 0, 0.8);
				at.size({ at.size().x * lvl, at.size().y });
				RenImm::get().draw_rect(at, (clr * (1 + 0.5 * (t_lvl - 1))).to_px());
				at.a.x = at.b.x;
				at.b.x = RenderControl::get_size().x - RenderControl::get_size().x/4;
				RenImm::get().draw_rect(at, (clr * t_lvl * 0.7).to_px());
				
				int secs = core.get_gmc().get_boot_left().seconds();
				RenImm::get().draw_text(ctr, FMT_FORMAT("{:3} seconds @ {:3}%", secs, int(100 * lvl)), 0x00c0c0ff, true);
			}
			
			// teleport menu / map
			
			if (!gctr.get_replay_reader()) {
				if (auto i_cur = core.get_info().get_menu_teleport())
				{
					pinp.set_context(PlayerInput::CTX_MENU);
					menu_pause = true;
					
					auto exit = [&]{
						pinp.set_context(PlayerInput::CTX_GAME);
						core.get_info().set_menu_teleport({});
						menu_pause = false;
					};
					
					if (core.get_pmg().get_ent())
					{
						const auto& list = core.get_info().get_teleport_list();
						auto sel = lmap->draw_transit(g_mpos, &list[*i_cur], list);
						
						vig_lo_toplevel({ {}, RenderControl::get_size(), true });
						for (auto& t : list)
							vig_label_a("{}, status: {}\n", t.room.name, t.discovered ? "Online" : "UNKNOWN");
						vig_lo_pop();
						
						auto& pcst = pinp.get_state(PlayerInput::CTX_MENU);
						if (pcst.is[PlayerInput::A_MENU_EXIT] ||
						    pcst.is[PlayerInput::A_INTERACT])
						{
							exit();
						}
						else if (pcst.is[ PlayerInput::A_MENU_SELECT ])
						{
							if (sel) {
								if (auto rw = gctr.get_replay_writer())
									rw->add_event(Replay_UseTransitTeleport{ sel->ent.index });
								
								sel->ent.teleport_player();
								exit();
							}
						}
					}
					else exit();
				}
				else
				{
					std::optional<vec2fp> plr_p;
					if (auto ent = core.get_pmg().get_ent()) plr_p = ent->get_pos();
					lmap->draw(passed, plr_p, inpst.is[PlayerInput::A_SHOW_MAP]);
					menu_pause = inpst.is[PlayerInput::A_SHOW_MAP];
				}
			}
			
			// object highlight
			
			if (auto plr = core.get_pmg().get_ent();
				plr && inpst.is[PlayerInput::A_HIGHLIGHT])
			{
				RenImm::get().set_context(RenImm::DEFCTX_WORLD);
				
				if (is_tutorial) {
					core.foreach([](auto& ent){
						if (typeid(ent) == typeid(ETutorialMsg)) {
							RenImm::get().draw_circle(ent.get_pos(), 1,   0xff40'40c0, 16);
							RenImm::get().draw_circle(ent.get_pos(), 0.5, 0x60ff'80ff, 8);
						}
					});
				}
				else {
					auto area = Rectfp::from_center(plr->get_pos(), vec2fp::one(36));
					core.get_phy().query_aabb(area, [&](Entity& ent, b2Fixture& fix)
					{
						auto allow_sensor = [&]{
							return typeid(ent) == typeid(EPickable)
								|| typeid(ent) == typeid(EDispenser)
								|| typeid(ent) == typeid(ETeleport)
								|| typeid(ent) == typeid(EMinidock);
						};
						auto allow_never = [&]{
							return typeid(ent) == typeid(EWall)
								|| typeid(ent) == typeid(EDoor);
						};
						if (fix.IsSensor()) {
							if (!allow_sensor())
								return;
						}
						else if (allow_never())
							return;
						
						float dist;
						if (fix.IsSensor()) {
							auto r = core.get_phy().raycast_nearest(conv(plr->get_pos()), conv(ent.get_pos()));
							if (r) return;
							dist = (ent.get_pos() - plr->get_pos()).fastlen();
						}
						else {
							auto d = core.get_phy().los_check(plr->get_pos(), ent, 3);
							if (!d) return;
							dist = *d;
						}
						
						std::string info;
						info.reserve(1024);
						bool show_hp = false;
						
						if (ent.ui_descr) info += ent.ui_descr;
						else info += "Unknown";
						info += FMT_FORMAT("\nDist: {:.1f}\nSpeed: {:.0f}\n", dist, ent.ref_pc().get_vel().fastlen());
						
						if (ent.is_creature())
						{
							if (ent.get_hlc())
								show_hp = true;
							
							if (auto drone = ent.get_ai_drone()) {
								if (std::holds_alternative<AI_Drone::Battle>(drone->get_state()))
									info += "State: COMBAT\n";
								else if (std::holds_alternative<AI_Drone::Idle>(drone->get_state()))
									info += "State: idle\n";
								else
									info += "State:\n";
							}
						}
						else {
							if (auto hc = ent.get_hlc(); hc && hc->get_hp().t_state() < 0.99)
								show_hp = true;
						}
						
						vec2fp dir = (ent.get_pos() - plr->get_pos()).norm();
						RenAAL::get().draw_line(
							plr->get_pos() + dir * plr->ref_pc().get_radius(),
							plr->get_pos() + dir * (dist - ent.ref_pc().get_radius()),
							FColor(0, 0.8, 0.6, 0.5).to_px(), 0.2, 1.5);
						
//						auto& b_aabb = fix.GetAABB(0);
//						Rectfp aabb = {conv(b_aabb.lowerBound), conv(b_aabb.upperBound), false};
//						aabb.offset(ent.get_pos());
						Rectfp aabb = Rectfp::from_center(ent.get_pos(), vec2fp::one(ent.ref_pc().get_radius()));
						
						RenImm::get().draw_frame(aabb, 0x00ff'4080, 0.2);
						RenImm::get().draw_text({aabb.lower().x, aabb.upper().y + 0.5f}, info, 0xc0ff'ffc0);
						
						if (show_hp)
						{
							float ht = (RenText::get().line_height(FontIndex::Mono) + 2) /
									   RenderControl::get().get_world_camera().get_state().mag;
							vec2fp at = aabb.lower();
							float x100 = 2;
							
							auto draw = [&](HealthPool& hp, int type){
								std::string str;
								uint32_t clr = 0;
								float t, wid;
								
								if (type == -1) {
									str = "Unknown";
									clr = 0xff0000;
									t = 1;
									wid = 2 * x100;
								}
								else {
									str = std::to_string(hp.exact().first);
									if		(type == 0) clr = 0xc0c000;
									else if (type == 1) clr = 0x2020ff;
									else if (type == 2) clr = 0x00ff40;
									t = hp.t_state();
									wid = x100 * hp.exact().second / 100;
								}
								
								at.y -= ht;
								clr <<= 8;
								float x1 = t * wid;
								RenImm::get().draw_rect({at, {x1, ht}, true}, clr | 0xa0);
								RenImm::get().draw_rect({{at.x + x1, at.y}, {wid - x1, ht}, true}, clr | 0x60);
								RenImm::get().draw_text(at, str, 0xffff'ffc0);
							};
							
							auto& hc = ent.ref_hlc();
							draw(hc.get_hp(), 0);
							hc.foreach_filter([&](DamageFilter& ft){
								if      (auto f = dynamic_cast<DmgShield*>(&ft)) draw(f->get_hp(), 1);
								else if (auto f = dynamic_cast<DmgArmor *>(&ft)) draw(f->get_hp(), 2);
								else draw(hc.get_hp(), -1);
							});
						}
					});
				}
				
				RenImm::get().set_context(RenImm::DEFCTX_UI);
			}
			
			// debug UI
			
			const float X_OFF = 200;
			const float Y_STR = RenText::get().line_height(FontIndex::Mono);
			bool l_but = (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK);
			bool r_but = (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_RMASK);
			
			if (ui_dbg_mode)
			{
				Entity* plr_ent = core.get_pmg().get_ent();
				
				draw_text_hud({X_OFF, RenderControl::get_size().y / 2.f}, "DEBUG UI MODE");
				if (ui_dbg_puppet_switch)
					draw_text_hud({X_OFF, RenderControl::get_size().y / 2.f + Y_STR}, "PUPPET MODE");
				
				// player teleport
				if (plr_ent && r_but && !ui_dbg_puppet_switch && allow_cheats)
				{
					const auto p = inpst.tar_pos;
					const vec2i gp = core.get_lc().to_cell_coord(p);
					if (Rect({1,1}, core.get_lc().get_size() - vec2i::one(2), false).contains_le( gp ))
						plr_ent->ref_phobj().teleport(p);
					
					if (auto rw = gctr.get_replay_writer())
						rw->add_event(Replay_DebugTeleport{ p });
				}
				
				// select object
				if (l_but /*&& !ui_dbg_puppet_switch*/)
				{
					Entity* lookat = nullptr;
					
					core.get_phy().query_aabb(
						Rectfp::from_center(inpst.tar_pos, vec2fp::one(0.3)),
					[&](Entity& ent, b2Fixture& fix)
					{
						if (typeid(ent) == typeid(EWall)) return true;
						if (fix.IsSensor() && ent.ref_phobj().is_material()) return true;
						
						lookat = &ent;
						return false;
					});
					
					if (lookat) dbg_select = lookat->index;
					else dbg_select = {};
				}
			}
			
			// object info + AI puppet
			if (auto ent = core.valid_ent(dbg_select))
			{
				std::string s;
				s += FMT_FORMAT("EID: {}\n", ent->index.to_int());
				
				if (ent->dbg_is_reg())
					s += "HASSTEPREG\n";
				
				if (auto h = ent->get_hlc()) {
					s += FMT_FORMAT("HP: {}/{}\n", h->get_hp().exact().first, h->get_hp().exact().second);
					h->foreach_filter([&](DamageFilter& f){
						if (auto p = dynamic_cast<DmgShield*>(&f)) {
							s += FMT_FORMAT("-- SHL {}/{}\n", p->get_hp().exact().first, p->get_hp().exact().second);
						}
						else s += "-- FLT\n";
					});
				}
				
				if (AI_Drone* d = ent->get_ai_drone()) s += d->get_dbg_state();
				else s += "NO DEBUG STATS\n";
				
				draw_text_hud({X_OFF, RenderControl::get_size().y / 2.f + Y_STR*4}, s);
				
				auto& cam = RenderControl::get().get_world_camera();
				auto pos  = cam.direct_cast( ent->get_pos() );
				auto size = cam.direct_cast( ent->get_pos() + vec2fp::one(ent->ref_pc().get_radius()) );
				RenImm::get().draw_frame( Rectfp::from_center(pos, size - pos), 0x00ff00ff, 3 );
				
				if (AI_Drone* d = ent->get_ai_drone();
				    d && allow_cheats && ui_dbg_mode)
				{
					if (ui_dbg_puppet_switch)
					{
						if (auto st = std::get_if<AI_Drone::Puppet>(&d->get_state())) {
							if (r_but)
								st->mov_tar = { inpst.tar_pos, AI_Speed::Normal };
						}
						else {
							d->set_idle_state();
							d->add_state( AI_Drone::Puppet{} );
						}
					}
					else if (std::holds_alternative<AI_Drone::Puppet>(d->get_state()))
						d->remove_state();
				}
			}
		}
	}
	void enable_debug_mode()
	{
		allow_cheats = true;
		pmg_ui->debug_mode = allow_cheats;
		pmg_ui->message("CHEATS ENABLED", TimeSpan::seconds(1), TimeSpan::seconds(1));
	}
	
	void save_automap(const char *filename)
	{
		const bool show_passable = true;
		
		auto lock = gctr.core_lock();
		auto& core = gctr.get_core();
		
		vec2fp size = core.get_lc().get_size();
		size *= GameConst::cell_size;
		
		auto& cam = RenderControl::get().get_world_camera();
		auto orig_frm = cam.get_state();
		cam.set_vport_full();
		
		Camera::Frame frm;
		
		frm.mag = 0.05;
		cam.set_state(frm);
		
		if (show_passable)
		{
			auto& lc = core.get_lc();
			vec2i sz = lc.get_size();
			
			for (int y=0; y < sz.y; ++y)
			for (int x=0; x < sz.x; ++x)
			{
				auto& c = lc.cref({x,y});
				if (!c.is_wall) {
					auto r = Rectfp::from_center(lc.to_center_coord({x,y}), vec2fp::one(GameConst::cell_size /2));
					GamePresenter::get()->dbg_rect(r, 0x00ff0040);
				}
			}
		}
		GamePresenter::get()->sync( TimeSpan::since_start() );
		
		frm.mag = 8;
		cam.set_state(frm);
		
		vec2i tex_sz = RenderControl::get_size();
		std::unique_ptr<Texture> tex( Texture::create_empty( tex_sz, Texture::FMT_RGBA ) );
		Postproc::get().capture_begin( tex.get() );
		
		vec2fp cam_sz = cam.coord_size();
		vec2i count = (size / cam_sz).int_ceil();
		vec2i ipos;
		
		VLOGD("AUTOMAP: map: {}x{}", size.x, size.y);
		VLOGD("         cam: {}x{}", cam_sz.x, cam_sz.y);
		VLOGD("         img: {}x{}", count.x, count.y);
		
		ImageInfo img, tmp;
		img.reset( tex_sz * count, ImageInfo::FMT_RGBA );
		
		size_t log_total = count.area();
		size_t log_count = 0;
		
		for (frm.pos.y = cam_sz.y /2, ipos.y = 0 ; frm.pos.y < size.y + cam_sz.y /2 && ipos.y < count.y ; frm.pos.y += cam_sz.y, ++ipos.y)
		for (frm.pos.x = cam_sz.x /2, ipos.x = 0 ; frm.pos.x < size.x + cam_sz.x /2 && ipos.x < count.x ; frm.pos.x += cam_sz.x, ++ipos.x)
		{
			RenImm::get().set_context(RenImm::DEFCTX_WORLD);
			
			cam.set_state(frm);
			GamePresenter::get()->render( TimeSpan::since_start(), {} );
			RenderControl::get().render({});
			
			Texture::debug_save(tex->get_obj(), tmp, Texture::FMT_RGBA);
			tmp.vflip();
			img.blit(ipos * tex_sz, tmp, {}, tex_sz);
			
			VLOGV("AUTOMAP {}/{} {}x{}", log_count, log_total, ipos.x, ipos.y);
			++log_count;
		}
		
		auto png_level = ImageInfo::png_compression_level;
		ImageInfo::png_compression_level = 0;
		img.convert(ImageInfo::FMT_RGB);
		img.save(filename);
		ImageInfo::png_compression_level = png_level;
		
		Postproc::get().capture_end();
		cam.set_state(orig_frm);
	}
};
GameUI* GameUI::create(InitParams pars) {
	return new GameUI_Impl(std::move(pars));
}



std::string GameUI::generate_greet()
{
	auto& rnd = rnd_stat();
	rnd.set_seed( std::hash<std::string>{}(date_time_str()) );
	
	std::vector<std::string> vs;
	if (rnd.range_n() < 0.3)
	{
		return {};
	}
	else if (rnd.range_n() < 0.8)
	{
		vs.emplace_back("Incrementing headcrabs...");
		vs.emplace_back("Reversing linked lists...");
		vs.emplace_back("Summoning CPU spirits...");
		vs.emplace_back("Converting walls to zombies...");
		vs.emplace_back("Uploading browser history...");
		vs.emplace_back("SPAM SPAM SPAM SPAM\nSPAM SPAM SPAM SPAM\nLovely spam!\nWonderful spam!");
		vs.emplace_back("Cake isn't implemented yet");
	}
	else
	{
		vs.emplace_back("///\\.oOOo./\\\\\\");
		vs.emplace_back("(;;)");
		vs.emplace_back("()_.._()");
	}
	return rnd.random_el(vs);
}
