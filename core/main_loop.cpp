#include <atomic>
#include <mutex>
#include <thread>
#include "client/level_map.hpp"
#include "client/plr_control.hpp"
#include "client/presenter.hpp"
#include "core/settings.hpp"
#include "core/vig.hpp"
#include "game/game_core.hpp"
#include "game/level_ctr.hpp"
#include "game/level_gen.hpp"
#include "game/player_mgr.hpp"
#include "game_ai/ai_group.hpp"
#include "game_objects/s_objs.hpp"
#include "render/camera.hpp"
#include "render/control.hpp"
#include "render/postproc.hpp"
#include "render/ren_imm.hpp"
#include "utils/noise.hpp"
#include "utils/res_image.hpp"
#include "utils/time_utils.hpp"
#include "vaslib/vas_file.hpp"
#include "vaslib/vas_log.hpp"
#include "main_loop.hpp"



class ML_Settings : public MainLoop
{
public:
	bool is_game;
	std::shared_ptr<PlayerController> ctr;
	std::optional<std::pair<int, int>> bix;
	vec2i scroll_pos = {};
	std::string gpad_try;
	
	void init()
	{
		is_game = !!ctr;
		if (!ctr) {
			ctr.reset (new PlayerController);
			ctr->set_gpad( std::unique_ptr<Gamepad> (Gamepad::open_default()) );
		}
	}
	void on_event(const SDL_Event& ev)
	{
		if		(ev.type == SDL_KEYUP)
		{
			int k = ev.key.keysym.scancode;
			if		(k == SDL_SCANCODE_ESCAPE)
			{
				if (bix) bix.reset();
				else delete this;
			}
		}
	}
	void render(TimeSpan)
	{
		vig_lo_push_scroll( RenderControl::get_size() - vig_element_decor()*2, scroll_pos );
		
		vig_label("KEYBINDS (hover over action to see description)\n");
		if (vig_button(is_game? "Return to game" : "Return to main menu")) {
			delete this;
			return;
		}
		vig_space_line();
		vig_lo_next();
		
		auto& bs = ctr->binds_ref();
		vigTableLC lc( vec2i(6, bs.size() + 1) );
		
		lc.get({0,0}).str = "ACTION";
		lc.get({1,0}).str = "KEY    ";
		lc.get({2,0}).str = "KEY    ";
		lc.get({3,0}).str = "MOUSE  ";
		lc.get({4,0}).str = "GAMEPAD";
		lc.get({5,0}).str = "TYPE   ";
		
		const char *hd_str[] = {
		    "",
		    "Primary key",
		    "Alternate key",
		    "Mouse button",
		    "Gamepad button",
		    "Action type"
		};
		
		for (size_t i=0; i<bs.size(); ++i)
		{
			lc.get( vec2i(0, i+1) ).str = bs[i].name;
			auto& is = bs[i].ims;
			for (size_t j=0; j<is.size(); ++j)
				lc.get( vec2i(j+1, i+1) ).str = is[j]->name.str;
			
			auto& s = lc.get( vec2i(5, i+1) ).str;
			switch (bs[i].type)
			{
			case PlayerController::BT_ONESHOT:
				s = "Trigger";
				break;
				
			case PlayerController::BT_HELD:
				s = "Hold";
				break;
				
			case PlayerController::BT_SWITCH:
				s = "Switch";
				break;
			}
		}
		
		vec2i off = vig_lo_get_next();
		lc.calc();
		
		for (int y=0; y < lc.get_size().y; ++y)
		for (int x=0; x < lc.get_size().x; ++x)
		{
			auto& e = lc.get({x,y});
			if (!y || !x) {
				vig_label(*e.str, off + e.pos, e.size);
				if (y) vig_tooltip( bs[y-1].descr, off + e.pos, e.size );
				else vig_tooltip( hd_str[x], off + e.pos, e.size );
			}
			else if (x == 5)
			{
				if (vig_button(*e.str, 0, false, false, off + e.pos, e.size))
					{;}
				switch (bs[y-1].type)
				{
				case PlayerController::BT_ONESHOT:
					vig_tooltip("Triggered on press", off + e.pos, e.size);
					break;
					
				case PlayerController::BT_HELD:
					vig_tooltip("Enabled while pressed", off + e.pos, e.size);
					break;
					
				case PlayerController::BT_SWITCH:
					vig_tooltip("Switched on press", off + e.pos, e.size);
					break;
				}
			}
			else {
				bool act = bix? y == bix->first + 1 && x == bix->second + 1 : false;
				if (vig_button(*e.str, 0, act, false, off + e.pos, e.size) && !bix)
					{;} //bix = std::make_pair(y-1, x-1);
			}
		}
		
		if (auto gpad = ctr->get_gpad())
		{
			if (gpad->get_gpad_state() == Gamepad::STATE_OK)
			{
				auto gc = static_cast<SDL_GameController*> (gpad->get_raw());
				
				vig_lo_next();
				vig_label_a("Your gamepad: {}\n", SDL_GameControllerName(gc));
				
				bool any = false;
				for (size_t i=0; i<Gamepad::TOTAL_BUTTONS_INTERNAL; ++i)
				{
					auto b = static_cast<Gamepad::Button>(i);
					if (gpad->get_state(b)) {
						auto nm = PlayerController::IM_Gpad::get_name(b);
						vig_label_a("Pressed: {}\n", nm.str);
						any = true;
					}
				}
				if (!any)
					vig_label("Press any gamepad button to see it's name");
			}
			else if (gpad->get_gpad_state()== Gamepad::STATE_WAITING)
				vig_label("Press START to init gamepad");
			else if (gpad->get_gpad_state()== Gamepad::STATE_DISABLED)
				vig_label("Gamepad is removed or disabled");
		}
		else {
			vig_lo_next();
			vig_label_a("No gamepad connected");
			
			vig_lo_next();
			if (vig_button("Enable gamepad"))
			{
				std::unique_ptr<Gamepad> gpad(Gamepad::open_default());
				if (!gpad) gpad_try = "Not found";
				else ctr->set_gpad( std::move(gpad) );
			}
			vig_label(gpad_try);
		}
	}
};



class ML_Game : public MainLoop
{
public:
	std::unique_ptr<GamePresenter> pres;
	std::unique_ptr<LevelControl> lvl;
	std::unique_ptr<AI_Controller> ai_ctr;
	std::unique_ptr<GameCore> core;
	std::shared_ptr<PlayerController> pc_ctr;
	
	std::atomic<bool> thr_term = false;
	std::thread thr_serv;
	std::mutex ren_lock;

	struct GP_Init
	{
		GamePresenter::InitParams gp;
	};
	std::optional<GP_Init> gp_init;
	bool game_fin = false;
	
	bool use_gamepad = false;
	
	bool ph_debug_draw = false;
	vigAverage dbg_serv_avg;
	RAII_Guard dbg_serv_g;
	
	double serv_avg_total = 0;
	size_t serv_avg_count = 0;
	
	std::unique_ptr<LevelMap> lmap;
	
	bool is_far_cam = false;
	TimeSpan cam_telep_tmo;
	
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
	std::vector<WinrarAnim> winrars;
	
	std::atomic<bool> pause_logic = false; ///< Control
	std::atomic<bool> pause_logic_ok = false; ///< Is really paused
	bool is_ren_paused = false; ///< Is paused be window becoming non-visible
	
	bool is_first_frame = false;
	
	
	
	ML_Game() = default;
	bool parse_arg(ArgvParse& arg)
	{
		if		(arg.is("--gpad-on"))  use_gamepad = true;
		else if (arg.is("--gpad-off")) use_gamepad = false;
		else if (arg.is("--cheats")) PlayerController::allow_cheats = true;
		else return false;
		return true;
	}
	void init()
	{
		// controls
		
		std::unique_ptr<Gamepad> gpad;
		if (use_gamepad)
		{
			TimeSpan time0 = TimeSpan::since_start();
			gpad.reset(Gamepad::open_default());
			VLOGI("Wasted on gamepad: {:.3f}", (TimeSpan::since_start() - time0).seconds());
		}
		else VLOGI("Gamepad is disabled by default");
		
		pc_ctr.reset (new PlayerController);
		pc_ctr->set_gpad(std::move(gpad));
		
		// dbg info
		
		dbg_serv_avg.reset(5.f, GameCore::time_mul);
		dbg_serv_g = vig_reg_menu(VigMenu::DebugGame, [this]
		{
			std::unique_lock lock(ren_lock);
			if (!pres) return;
			
			dbg_serv_avg.draw();
			vig_lo_next();
			
			vig_label_a("Raycasts:   {:4}\n", GameCore::get().get_phy().raycast_count);
			vig_label_a("AABB query: {:4}\n", GameCore::get().get_phy().aabb_query_count);
			vig_lo_next();
			
			if (PlayerController::allow_cheats)
			{
				vig_checkbox(GameCore::get().dbg_ai_attack, "AI attack");
				vig_lo_next();
			}
			
			if (auto ent = core->get_pmg().get_ent())
			{
				auto pos = ent->get_phy().get_pos();
				vig_label_a("x:{:5.2f} y:{:5.2f}", pos.x, pos.y);
				vig_lo_next();
				
				if (vig_button("Damage self"))
					ent->get_hlc()->apply({ DamageType::Direct, 30 });
				
				if (PlayerController::allow_cheats)
				{
					bool upd_cheats = false;
					upd_cheats |= vig_checkbox(core->get_pmg().cheat_ammo, "Infinite ammo");
					upd_cheats |= vig_checkbox(core->get_pmg().cheat_godmode, "God mode");
					if (upd_cheats) core->get_pmg().update_cheats();
				}
				else vig_label("Cheats disabled");
			}
		});
		
		// init
		
		thr_serv = std::thread([this]{thr_func();});
	}
	~ML_Game()
	{
		thr_term = true;
		if (thr_serv.joinable())
			thr_serv.join();
		
		VLOGI("Average logic frame length: {} ms, {} samples", serv_avg_total, serv_avg_count);
	}
	void on_event(const SDL_Event& ev)
	{
		if (ev.type == SDL_KEYUP) {
			int k = ev.key.keysym.scancode;
			
			if (is_ren_paused) {
				pause_logic = false;
				is_ren_paused = false;
				return;
			}
			
			if		(k == SDL_SCANCODE_0) ph_debug_draw = !ph_debug_draw;
			else if (k == SDL_SCANCODE_ESCAPE || k == SDL_SCANCODE_F10)
			{
				MainLoop::create(INIT_SETTINGS);
				dynamic_cast<ML_Settings&>(*MainLoop::current).ctr = pc_ctr;
				MainLoop::current->init();
			}
			else if (k == SDL_SCANCODE_B) is_far_cam = !is_far_cam;
			else if (k == SDL_SCANCODE_F4)
			{
				auto& pm = core->get_pmg();
				pm.cheat_godmode = !pm.cheat_godmode;
				pm.update_cheats();
			}
			else if (k == SDL_SCANCODE_F5) save_automap("AUTOMAP.png");
		}
		
		if (!pause_logic) {
			auto g = pc_ctr->lock();
			pc_ctr->on_event(ev);
		}
	}
	void render(TimeSpan passed)
	{
		std::unique_lock lock(ren_lock);
		if (thr_term) throw std::runtime_error("Game failed");
		if (!pres)
		{
			draw_text_message("Generating...");
			
			if (gp_init)
			{
				auto time0 = TimeSpan::since_start();
				
				GamePresenter::init(gp_init->gp);
				pres.reset( GamePresenter::get() );
				gp_init.reset();
				
				lmap->ren_init();
				
				VLOGI("Game render init finished in {:.3f} seconds", (TimeSpan::since_start() - time0).seconds());
				log_write_str(LogLevel::Critical,
				              FMT_FORMAT("Full init took {:.3f} seconds", TimeSpan::since_start().seconds()).data());
				
				Postproc::get().tint_seq({}, FColor(0,0,0,0));
				Postproc::get().tint_seq(TimeSpan::seconds(30), FColor(0,0,0,0));
			}
		}
		else if (game_fin)
		{
			winrars.resize(40);
			for (auto& w : winrars) w.draw();
			draw_text_message("Game completed.\n\nA WINRAR IS YOU.");
		}
		else if (!RenderControl::get().is_visible())
		{
			pause_logic = true;
			is_ren_paused = true;
		}
		else {
//#warning Debug exit
//			save_automap("AUTOMAP.png"); throw std::logic_error("Debug exit");
			
			if (is_first_frame && core->get().get_step_time().is_positive())
			{
				VLOGI("First game frame rendered at {:.3f} seconds", TimeSpan::since_start().seconds());
				is_first_frame = false;
				Postproc::get().tint_default(TimeSpan::seconds(3));
			}
			
			// set camera
			
			if (auto ent = core->get_pmg().get_ent())
			{
				auto& cam = RenderControl::get().get_world_camera();
				
				vec2fp cam_size_m;
				if (pc_ctr->get_state().is[ PlayerController::A_CAM_CLOSE_SW ]) cam_size_m = {60, 35};
				else cam_size_m = {60, 50};
				float calc_mag = (vec2fp(RenderControl::get().get_size()) / cam_size_m).minmax().x;
				
				const float tar_min = GameConst::hsz_rat * 2;
				const float tar_max = 15.f * (calc_mag / cam.get_state().mag);
				const float per_second = 0.08 / TimeSpan::fps(60).seconds();
				
				const vec2fp pos = ent->get_phy().get_pos();
				vec2fp tar = pos;
				
				auto& pctr = pc_ctr->get_state();
				if (pctr.is[PlayerController::A_CAM_FOLLOW])
				{
					tar = pctr.tar_pos;
					
					vec2fp tar_d = (tar - pos);
					if		(tar_d.len() < tar_min) tar = pos;
					else if (tar_d.len() > tar_max) tar = pos + tar_d.get_norm() * tar_max;
				}
				
				auto frm = cam.get_state();
				if (is_far_cam) calc_mag /= 1.5;
				else calc_mag *= AppSettings::get().cam_mag_mul;
				frm.mag = calc_mag;
				
				const vec2fp scr = cam.coord_size();
				const vec2fp tar_d = (tar - frm.pos);
				
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
						frm.pos = tar;
						cam.set_state(frm);
					}
					else cam.set_state(frm);
				}
				else
				{
					vec2fp dt = (tar - frm.pos) * (per_second * passed.seconds());
					
					cam_telep_tmo = {};
					frm.pos += dt;
					cam.set_state(frm);
				}
			}
			
			// draw world
			
			RenImm::get().set_context(RenImm::DEFCTX_WORLD);
			GamePresenter::get()->render(passed);
			
			if (ph_debug_draw)
				core->get_phy().world.DrawDebugData();
			
			// draw pause stub
			
			if (pause_logic || pause_logic_ok)
			{
				RenImm::get().set_context(RenImm::DEFCTX_UI);
				draw_text_message("PAUSED\nPress any key to continue");
				return;
			}
			
			// draw UI
			
			RenImm::get().set_context(RenImm::DEFCTX_UI);
			
			if (vig_current_menu() == VigMenu::Default)
			{
				vec2i mpos;
				SDL_GetMouseState(&mpos.x, &mpos.y);
				core->get_pmg().render(passed, mpos);
			}
			
			std::optional<vec2fp> plr_p;
			if (auto ent = core->get_pmg().get_ent()) plr_p = ent->get_phy().get_pos();
			lmap->draw(passed, plr_p, pc_ctr->get_state().is[PlayerController::A_SHOW_MAP]);
		}
	}
	
	
	
	void init_game()
	{	
		TimeSpan t0 = TimeSpan::since_start();
		
		RandomGen lt_rnd; // 260,120 180,100
		std::shared_ptr<LevelTerrain> lt( LevelTerrain::generate({ &lt_rnd, {220,140}, 3 }) );
//#warning Debug exit
//		lt->test_save(); throw std::logic_error("Debug exit");
		
		GameCore::InitParams gci;
		gci.pmg.reset( PlayerManager::create(pc_ctr) );
		
		ai_ctr.reset( AI_Controller::init() );
		lmap.reset( LevelMap::init(*lt) );
		core.reset( GameCore::create( std::move(gci) ) );
		lvl.reset( LevelControl::init(*lt) );
		
		gp_init = {{lt}};
		while (!pres)
		{
			if (thr_term) throw std::runtime_error("init_game() interrupted");
			sleep(TimeSpan::fps(30));
		}
		
		new EWall(lt->ls_wall);
		lvl->fin_init(*lt);
		
		VLOGI("init_game() finished in {:.3f} seconds", (TimeSpan::since_start() - t0).seconds());
	}
	
	void thr_func()
	try
	{
		init_game();
		VLOGI("Game initialized");
		
		while (!thr_term)
		{
			auto t0 = TimeSpan::since_start();
			if (MainLoop::current == this)
			{
				pause_logic_ok = pause_logic.load();
				if (!pause_logic)
				{
					std::unique_lock lock(ren_lock);
					core->step();
					ai_ctr->step();
				}
				
				if (GameCore::get().get_pmg().is_game_finished())
				{
					game_fin = true;
					break;
				}
			}
			
			auto dt = TimeSpan::since_start() - t0;
			sleep(core->step_len - dt);
			
			dbg_serv_avg.add (dt.seconds());
			if (!pause_logic_ok) {
				++serv_avg_count;
				serv_avg_total += (dt.seconds() * 1000 - serv_avg_total) / serv_avg_count;
			}
		}
	}
	catch (std::exception& e) {
		thr_term = true;
		VLOGE("Game failed: {}", e.what());
	}
	
	
	
	void save_automap(const char *filename)
	{
		pause_logic = true;
		while (!pause_logic_ok) sleep(TimeSpan::ms(5));
		
		vec2fp size = LevelControl::get().get_size();
		size *= LevelControl::get().cell_size;
		
		auto& cam = RenderControl::get().get_world_camera();
		auto orig_frm = cam.get_state();
		cam.set_vport_full();
		
		Camera::Frame frm;
		
		frm.mag = 0.05;
		cam.set_state(frm);
		GamePresenter::get()->sync();
		
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
			GamePresenter::get()->render({});
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
		pause_logic = false;
	}
};



MainLoop* MainLoop::current;

void MainLoop::create(InitWhich which)
{
	MainLoop* prev = current;
	
	try {
		switch (which)
		{
		case INIT_DEFAULT:
		case INIT_GAME:
			current = new ML_Game;
			break;
			
		case INIT_SETTINGS:
			current = new ML_Settings;
			break;
		}
	}
	catch (std::exception& e) {
		THROW_FMTSTR("MainLoop::create() failed: {}", e.what());
	}
	
	current->ml_prev = prev;
}
MainLoop::~MainLoop() {
	if (current == this) current = ml_prev;
}
bool MainLoop::parse_arg(ArgvParse&) {return false;}
