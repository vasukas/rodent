#include "game_ui.hpp"

#include "client/ec_render.hpp"
#include "client/level_map.hpp"
#include "client/player_ui.hpp"
#include "client/plr_control.hpp"
#include "client/presenter.hpp"
#include "client/replay.hpp"
#include "core/hard_paths.hpp"
#include "core/vig.hpp"
#include "game/game_core.hpp"
#include "game/player_mgr.hpp"
#include "render/camera.hpp"
#include "render/control.hpp"
#include "render/ren_aal.hpp"
#include "render/ren_imm.hpp"
#include "render/texture.hpp"
#include "vaslib/vas_log.hpp"

#include "game/damage.hpp"
#include "game/game_info_list.hpp"
#include "game/physics.hpp"
#include "game_ai/ai_drone.hpp"
#include "game_objects/objs_basic.hpp"
#include "render/postproc.hpp"
#include "render/ren_text.hpp"
#include "utils/res_image.hpp"



struct WinrarAnim
{
	static const int off = 100;
	vec2fp pos, spd;
	FColor clr;
	
	WinrarAnim() {gen();}
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
		pos.x = rnd_stat().range(-off, RenderControl::get_size().x + off);
		pos.y = RenderControl::get_size().y + off;
		
		spd.x = rnd_stat().range_n2() * 70;
		spd.y = -rnd_stat().range(150, 400);
		
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
	std::shared_ptr<PlayerController> pc_ctr;
	
	std::optional<float> replay_speed_k;
	bool is_playback = false;
	
	// screen
	
	bool is_ren_paused = false; ///< Is paused be window becoming non-visible
	TimeSpan hide_pause_until; ///< Pause stub is not drawn until after that time
	bool is_first_frame = true;
	
	// interface
	
	CameraControl cctr;
	
	std::unique_ptr<LevelMap> lmap; ///< May be null
	std::unique_ptr<Texture> crosshair_tex;
	
	std::vector<WinrarAnim> winrars;
	std::string init_greet;
	
	// debug controls
	
	bool ph_debug_draw = false;
	
	bool ui_dbg_mode = false;
	EntityIndex dbg_select;
	bool ui_dbg_puppet_switch = false;
	
	// debug stats
	
	RAII_Guard dbg_serv_g;
	std::optional<vigAverage> dbg_serv_avg;
	
	double serv_avg_total = 0;
	size_t serv_avg_count = 0;
	size_t serv_overmax_count = 0;
	
	
	
	GameUI_Impl(InitParams pars)
		: gctr(*pars.ctr)
	{
		pc_ctr = std::move(pars.pc_ctr);
		init_greet = std::move(pars.init_greet);
		
		RenAAL::get().draw_grid = true;
		Postproc::get().tint_seq({}, FColor(0,0,0,0));
		
		crosshair_tex.reset(Texture::load(HARDPATH_CROSSHAIR_IMG));
		if (pars.lt)
			lmap.reset( LevelMap::init(*pars.lt) );
		
		if (pars.debug_menu)
		{
			dbg_serv_avg.emplace(5.f, GameCore::time_mul);
			
			dbg_serv_g = vig_reg_menu(VigMenu::DebugGame, [this]
			{
				auto lock = gctr.core_lock();
				auto& core = gctr.get_core();
				
				dbg_serv_avg->draw();
				vig_lo_next();
				
				vig_label_a("Raycasts:   {:4}\n", core.get_phy().raycast_count);
				vig_label_a("AABB query: {:4}\n", core.get_phy().aabb_query_count);
				vig_lo_next();
				
				if (PlayerController::allow_cheats)
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
					
					if (vig_button("Damage self"))
						ent->ref_hlc().apply({ DamageType::Direct, 30 });
					
					if (PlayerController::allow_cheats)
					{
						bool upd_cheats = false;
						upd_cheats |= vig_checkbox(core.get_pmg().cheat_ammo, "Infinite ammo");
						upd_cheats |= vig_checkbox(core.get_pmg().cheat_godmode, "God mode");
						upd_cheats |= vig_checkbox(core.get_pmg().is_superman, "Superman (requires respawn)");
						if (upd_cheats) core.get_pmg().update_cheats();
						
						vig_lo_next();
						if (vig_button("Get all keys")) {
							for (int i=0; i<3; ++i)
								core.get_pmg().inc_objective();
						}
					}
					else vig_label("Cheats disabled");
					vig_lo_next();
				}
			});
		}
		
		auto lock = gctr.core_lock();
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
		gctr.get_core().get_pmg().set_pui( std::unique_ptr<PlayerUI>(PlayerUI::create()) );
	}
	~GameUI_Impl()
	{
		gctr.set_post_step({});
		VLOGI("Average logic frame length: {} ms, {} samples", serv_avg_total, serv_avg_count);
		VLOGI("Logic frame length > sleep time: {} samples", serv_overmax_count);
	}
	void on_leave()
	{
		RenAAL::get().draw_grid = false;
		
		auto lock = gctr.core_lock();
		gctr.set_pause(true);
	}
	void on_enter()
	{
		RenAAL::get().draw_grid = true;
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
			if (!is_first_frame && PlayerController::allow_cheats)
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
			else if (!is_first_frame && PlayerController::allow_cheats)
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
				else if (k == SDL_SCANCODE_F5) {
					save_automap("AUTOMAP.png");
				}
				else if (k == SDL_SCANCODE_F7) {
					ui_dbg_mode = !ui_dbg_mode;
					ui_dbg_puppet_switch = false;
				}
				else if (k == SDL_SCANCODE_F9) {
					auto lock = gctr.core_lock();
					bool& flag = gctr.get_core().get_aic().show_aos_debug;
					flag = !flag;
				}
				else if (k == SDL_SCANCODE_F10) RenParticles::get().enabled = !RenParticles::get().enabled;
				else if (k == SDL_SCANCODE_LSHIFT || k == SDL_SCANCODE_RSHIFT) {
					ui_dbg_mode = false;
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
			auto g = pc_ctr->lock();
			pc_ctr->on_event(ev);
		}
	}
	void render(TimeSpan frame_time, TimeSpan passed)
	{
		auto gstate = gctr.get_state();
		if		(auto st = std::get_if<GameControl::CS_Init>(&gstate))
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
			GamePresenter::get()->render(frame_time, {});
			//
			RenImm::get().set_context(RenImm::DEFCTX_UI);
			RenImm::get().draw_rect({{}, RenderControl::get_size(), false}, 0xa0);
			
			winrars.resize(40);
			for (auto& w : winrars) w.draw();
			draw_text_message("Game completed.\n\nA WINRAR IS YOU.");
		}
		else if (!RenderControl::get().is_visible())
		{
			auto lock = gctr.core_lock();
			gctr.set_pause(true);
			is_ren_paused = true;
		}
		else
		{
			if (SDL_GetKeyboardFocus() != RenderControl::get().get_wnd())
				is_ren_paused = true;
			
			auto lock = gctr.core_lock();
			auto& core = gctr.get_core();
			
			bool is_core_paused = std::get<GameControl::CS_Run>(gctr.get_state()).paused;
			gctr.set_pause(is_ren_paused);
			gctr.set_speed(replay_speed_k);
			
			if (is_first_frame)
			{
				VLOGI("First game frame rendered at {:.3f} seconds", TimeSpan::since_start().seconds());
				is_first_frame = false;
			}
			
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
				
				auto& pctr = pc_ctr->get_state();
				if (pctr.is[PlayerController::A_CAM_FOLLOW])
				{
					tar = pctr.tar_pos;
					
					vec2fp tar_d = (tar - pos);
					if		(tar_d.len() < tar_min) tar = pos;
					else if (tar_d.len() > tar_max) tar = pos + tar_d.get_norm() * tar_max;
				}
				
				cctr.update(tar, passed);
			}
			
			// draw world
			
			RenImm::get().set_context(RenImm::DEFCTX_WORLD);
			GamePresenter::get()->render( frame_time, passed );
			
			if (ph_debug_draw)
				core.get_phy().world.DebugDraw();
			
			// draw pause stub
			
			if (is_core_paused)
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
			
			bool use_crosshair = is_playback
			                     || (pc_ctr->get_gpad() && pc_ctr->get_gpad()->get_gpad_state() == Gamepad::STATE_OK);
			vec2i g_mpos;
			if (use_crosshair) {
				g_mpos = RenderControl::get().get_world_camera().direct_cast( pc_ctr->get_state().tar_pos );
			}
			else SDL_GetMouseState(&g_mpos.x, &g_mpos.y);
			
			if (vig_current_menu() == VigMenu::Default && !core.get_info().get_menu_teleport())
			{
				core.get_pmg().render(passed, g_mpos);
				
				if (use_crosshair) // draw
				{
					float z = std::max(10., RenderControl::get_size().minmax().y * 0.03);
					uint32_t clr = 0x00ff20c0;
					
					vec2fp p = pc_ctr->get_state().tar_pos;
					p = RenderControl::get().get_world_camera().direct_cast(p);
					RenImm::get().draw_image(Rectfp::from_center(p, vec2fp::one(z/2)), crosshair_tex.get(), clr);
				}
			}
			
			// teleport menu
			
			if (auto i_cur = core.get_info().get_menu_teleport())
			{
				pc_ctr->set_menu_mode(PlayerController::MMOD_MENU);
				auto exit = [&]{
					pc_ctr->set_menu_mode(PlayerController::MMOD_DEFAULT);
					core.get_info().set_menu_teleport({});
				};
				
				if (core.get_pmg().get_ent())
				{
					const auto& list = core.get_info().get_teleport_list();
					auto sel = lmap->draw_transit(g_mpos, &list[*i_cur], list);
					
					vig_lo_toplevel({ {}, RenderControl::get_size(), true });
					for (auto& t : list)
						vig_label_a("{}, status: {}\n", t.room.name, t.discovered ? "Online" : "UNKNOWN");
					vig_lo_pop();
					
					if (pc_ctr->get_state().is[ PlayerController::A_MENU_EXIT ]) {
						exit();
					}
					else if (pc_ctr->get_state().is[ PlayerController::A_MENU_SELECT ])
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
				lmap->draw(passed, plr_p, pc_ctr->get_state().is[PlayerController::A_SHOW_MAP]);
			}
			
			// debug UI
			
			if (ui_dbg_mode)
			{
				const float X_OFF = 200;
				const float Y_STR = RenText::get().line_height(FontIndex::Mono);
				
				Entity* plr_ent = core.get_pmg().get_ent();
				
				bool l_but = (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK);
				bool r_but = (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_RMASK);
				
				draw_text_hud({X_OFF, RenderControl::get_size().y / 2.f}, "DEBUG UI MODE");
				if (ui_dbg_puppet_switch)
					draw_text_hud({X_OFF, RenderControl::get_size().y / 2.f + Y_STR}, "PUPPET MODE");
				
				// player teleport
				if (plr_ent && r_but && !ui_dbg_puppet_switch)
				{
					const auto p = pc_ctr->get_state().tar_pos;
					const vec2i gp = core.get_lc().to_cell_coord(p);
					if (Rect({1,1}, core.get_lc().get_size() - vec2i::one(2), false).contains_le( gp ))
						plr_ent->ref_phobj().teleport(p);
					
					if (auto rw = gctr.get_replay_writer())
						rw->add_event(Replay_DebugTeleport{ p });
				}
				
				// object info + AI puppet
				if (auto ent = core.valid_ent(dbg_select))
				{
					std::string s;
					s += FMT_FORMAT("EID: {}\n", ent->index.to_int());
					
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
					
					if (AI_Drone* d = ent->get_ai_drone())
					{
						if (ui_dbg_puppet_switch)
						{
							if (auto st = std::get_if<AI_Drone::Puppet>(&d->get_state())) {
								if (r_but)
									st->mov_tar = { pc_ctr->get_state().tar_pos, AI_Speed::Normal };
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
				
				// select object
				if (l_but /*&& !ui_dbg_puppet_switch*/)
				{
					Entity* lookat = nullptr;
					
					core.get_phy().query_aabb(
						Rectfp::from_center(pc_ctr->get_state().tar_pos, vec2fp::one(0.3)),
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
		}
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
