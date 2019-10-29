#include <mutex>
#include <thread>
#include "client/level_map.hpp"
#include "client/plr_control.hpp"
#include "client/presenter.hpp"
#include "core/vig.hpp"
#include "game/game_core.hpp"
#include "game/level_ctr.hpp"
#include "game/level_gen.hpp"
#include "game/player_mgr.hpp"
#include "game/s_objs.hpp"
#include "render/camera.hpp"
#include "render/control.hpp"
#include "render/postproc.hpp"
#include "render/ren_aal.hpp"
#include "render/ren_imm.hpp"
#include "utils/time_utils.hpp"
#include "vaslib/vas_log.hpp"
#include "main_loop.hpp"



struct Watchdog
{
	const TimeSpan max = TimeSpan::seconds(3);
	
	void reset() {
		std::unique_lock l(m);
		t = {};
	}
	void add(TimeSpan val) {
		std::unique_lock l(m);
		t += val;
		if (t > max) {
			VLOGC("!!!WATCHDOG!!!");
			log_terminate_h_reset();
			std::terminate();
		}
	}
	
private:
	std::mutex m;
	TimeSpan t;
};
static Watchdog wdog;



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
	void on_event(SDL_Event& ev)
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
		wdog.reset();
		
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
	std::unique_ptr<GameCore> core;
	std::shared_ptr<PlayerController> pc_ctr;
	
	bool thr_term = false;
	std::thread thr_serv;
	std::mutex ren_lock;

	struct GP_Init
	{
		GamePresenter::InitParams gp;
	};
	std::optional<GP_Init> gp_init;
	
	bool use_gamepad = false;
	
	bool ph_debug_draw = false;
	vigAverage dbg_serv_avg;
	RAII_Guard dbg_serv_g;
	
	std::unique_ptr<LevelMap> lmap;
	
	const float cam_default_mag = 18.f;
	TimeSpan cam_telep_tmo;
	
	
	
	ML_Game() = default;
	bool parse_arg(ArgvParse& arg)
	{
		if		(arg.is("--gpad-on"))  use_gamepad = true;
		else if (arg.is("--gpad-off")) use_gamepad = false;
		else return false;
		return true;
	}
	void init()
	{
		// camera
		
		Camera* cam = RenderControl::get().get_world_camera();
		Camera::Frame cf = cam->get_state();
		cf.mag = cam_default_mag;
		cam->set_state(cf);
		
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
			
			vig_checkbox(GameCore::get().dbg_ai_attack, "AI attack");
			vig_lo_next();
			
			if (auto ent = core->get_pmg().get_ent())
			{
				auto pos = ent->get_phy().get_pos();
				vig_label_a("x:{:5.2f} y:{:5.2f}", pos.x, pos.y);
				vig_lo_next();
				vig_checkbox(ent->get_eqp()->infinite_ammo, "Infinite ammo");
				if (vig_checkbox(core->get_pmg().god_mode, "God mode"))
					core->get_pmg().update_godmode();
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
	}
	void on_event(SDL_Event& ev)
	{
		if (ev.type == SDL_KEYUP) {
			int k = ev.key.keysym.scancode;
			
			if		(k == SDL_SCANCODE_0) ph_debug_draw = !ph_debug_draw;
			else if (k == SDL_SCANCODE_ESCAPE)
			{
				MainLoop::create(INIT_SETTINGS);
				dynamic_cast<ML_Settings&>(*MainLoop::current).ctr = pc_ctr;
				MainLoop::current->init();
			}
			else if (k == SDL_SCANCODE_B)
			{
				Camera* cam = RenderControl::get().get_world_camera();
				Camera::Frame cf = cam->get_state();
				cf.mag = aequ(cf.mag, cam_default_mag, 0.1) ? 10.f : cam_default_mag;
				cam->set_state(cf);
			}
			else if (k == SDL_SCANCODE_F4)
			{
				auto& pm = core->get_pmg();
				pm.god_mode = !pm.god_mode;
				pm.update_godmode();
			}
		}
		
		auto g = pc_ctr->lock();
		pc_ctr->on_event(ev);
	}
	void render(TimeSpan passed)
	{
		wdog.reset();
		
		std::unique_lock lock(ren_lock);
		if (thr_term) throw std::runtime_error("Game failed");
		if (!pres)
		{
			RenImm::get().draw_text(RenderControl::get_size() /2, "Generating...", -1, true, 4.f);
			
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
			}
		}
		else {
			// set camera
			
			if (auto ent = core->get_pmg().get_ent())
			{
				auto cam = RenderControl::get().get_world_camera();
				
				const float tar_min = GameConst::hsz_rat * 2;
				const float tar_max = 15.f * (cam_default_mag / cam->get_state().mag);
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
				
				auto frm = cam->get_state();
				
				const vec2fp scr = cam->coord_size();
				const vec2fp tar_d = (tar - frm.pos);
				
				if (std::fabs(tar_d.x) > scr.x || std::fabs(tar_d.y) > scr.y)
				{
					auto half = TimeSpan::seconds(0.3);
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
						cam->set_state(frm);
					}
				}
				else
				{
					vec2fp dt = (tar - frm.pos) * (per_second * passed.seconds());
					
					cam_telep_tmo = {};
					frm.pos += dt;
					cam->set_state(frm);
				}
			}
			
			// draw world
			
			RenImm::get().set_context(RenImm::DEFCTX_WORLD);
			GamePresenter::get()->render(passed);
			
			if (ph_debug_draw)
				core->get_phy().world.DrawDebugData();
			
			// draw UI
			
			RenImm::get().set_context(RenImm::DEFCTX_UI);
			
			if (vig_current_menu() == VigMenu::Default)
				core->get_pmg().render(passed);
			
			std::optional<vec2fp> plr_p;
			if (auto ent = core->get_pmg().get_ent()) plr_p = ent->get_phy().get_pos();
			lmap->draw(passed, plr_p, pc_ctr->get_state().is[PlayerController::A_SHOW_MAP]);
		}
	}
	
	
	
	void init_game()
	{	
		TimeSpan t0 = TimeSpan::since_start();
		
//		std::shared_ptr<LevelTerrain> lt( LevelTerrain::generate({ {260,120}, 3 }) );
		std::shared_ptr<LevelTerrain> lt( LevelTerrain::load_testlvl(3) );
//		lt->test_save();
//		exit(666);
		
		GameCore::InitParams gci;
		gci.pmg.reset( PlayerManager::create(pc_ctr) );
		
		lmap.reset( LevelMap::init(*lt) );
		core.reset( GameCore::create( std::move(gci) ) );
		lvl.reset( LevelControl::init(*lt) );
		
		gp_init = {{lt}};
		while (!pres)
		{
			if (thr_term) throw std::runtime_error("init_game() interrupted");
			
			TimeSpan st = TimeSpan::fps(30);
			sleep(st);
			wdog.add(st);
		}
		
		new EWall(lt->ls_wall);
		lvl->fin_init();
		
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
				std::unique_lock lock(ren_lock);
				core->step();
			}
			auto dt = TimeSpan::since_start() - t0;
			
			TimeSpan st = core->step_len - dt;
			sleep(st);
			wdog.add(st);
			
			dbg_serv_avg.add (dt.seconds());
		}
	}
	catch (std::exception& e) {
		thr_term = true;
		VLOGE("Game failed: {}", e.what());
	}
};



MainLoop* MainLoop::current;

MainLoop* MainLoop::create(InitWhich which)
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
	return current;
}
MainLoop::~MainLoop() {
	if (current == this) current = ml_prev;
}
bool MainLoop::parse_arg(ArgvParse&) {return false;}
