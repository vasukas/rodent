#include <future>
#include <mutex>
#include <thread>
#include "client/level_map.hpp"
#include "client/plr_control.hpp"
#include "client/presenter.hpp"
#include "core/vig.hpp"
#include "game/game_core.hpp"
#include "game/level_ctr.hpp"
#include "game/level_gen.hpp"
#include "game/player.hpp"
#include "game/s_objs.hpp"
#include "game/weapon.hpp"
#include "render/camera.hpp"
#include "render/control.hpp"
#include "render/particles.hpp"
#include "render/ren_aal.hpp"
#include "render/ren_imm.hpp"
#include "utils/noise.hpp"
#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_log.hpp"
#include "main_loop.hpp"



class ML_Settings : public MainLoop
{
public:
	bool is_game;
	std::shared_ptr<PlayerController> ctr;
	std::optional<std::pair<int, int>> bix;
	
	void init()
	{
		is_game = !!ctr;
		if (!ctr)
			ctr.reset (new PlayerController (std::unique_ptr<Gamepad> (Gamepad::open_default())));
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
		vig_label("KEYBINDS (hover over action to see description)\n");
		if (vig_button(is_game? "Return to game" : "Return to main menu")) {
			delete this;
			return;
		}
		vig_space_line();
		vig_lo_next();
		
		auto& bs = ctr->binds_ref();
		vigTableLC lc( vec2i(5, bs.size() + 1) );
		
		lc.get({0,0}).str = "ACTION";
		lc.get({1,0}).str = "KEY         ";
		lc.get({2,0}).str = "KEY         ";
		lc.get({3,0}).str = "MOUSE       ";
		lc.get({4,0}).str = "GAMEPAD     ";
		
		for (size_t i=0; i<bs.size(); ++i)
		{
			lc.get( vec2i(0, i+1) ).str = bs[i].name;
			auto& is = bs[i].ims;
			for (size_t j=0; j<is.size(); ++j)
				lc.get( vec2i(j+1, i+1) ).str = is[j]->name.str;
		}
		
		vec2i off = vig_lo_get_next();
		lc.calc();
		
		for (int y=0; y < lc.get_size().y; ++y)
		for (int x=0; x < lc.get_size().x; ++x)
		{
			auto& e = lc.get({x,y});
			if (!y || !x) {
				vig_label(*e.str, off + e.pos, e.size);
				if (y && !x) vig_tooltip( bs[y-1].descr, off + e.pos, e.size );
			}
			else {
				bool act = bix? y == bix->first + 1 && x == bix->second + 1 : false;
				if (vig_button(*e.str, 0, act, false, off + e.pos, e.size) && !bix)
					{;} //bix = std::make_pair(y-1, x-1);
			}
		}
		
		if (auto gpad = ctr->get_gpad())
		{
			auto gc = static_cast<SDL_GameController*> (gpad->get_raw());
			
			vig_lo_next();
			vig_label_a("Your controller: {}\n", SDL_GameControllerName(gc));
			
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
	
	EntityIndex plr_eid = {}; // player
	Entity* plr_ent = nullptr;
	
	bool ph_debug_draw = false;
	vigAverage dbg_serv_avg;
	RAII_Guard dbg_serv_g;
	
	std::unique_ptr<LevelMap> lmap;
	
	
	
	ML_Game() = default;
	void init()
	{
		Camera* cam = RenderControl::get().get_world_camera();
		Camera::Frame cf = cam->get_state();
		cf.mag = 18.f;
		cam->set_state(cf);
		
		pc_ctr.reset (new PlayerController (std::unique_ptr<Gamepad> (Gamepad::open_default())));
		VLOGI("Box2D version: {}.{}.{}", b2_version.major, b2_version.minor, b2_version.revision);
		
		thr_serv = std::thread([this]{thr_func();});
		
		dbg_serv_avg.reset(5.f, GameCore::time_mul);
		dbg_serv_g = vig_reg_menu(VigMenu::DebugGame, [this]
		{
			std::unique_lock lock(ren_lock);
			dbg_serv_avg.draw();
			vig_lo_next();
			
			if (plr_ent)
			{
				auto pos = plr_ent->get_phy().get_pos();
				vig_label_a("x:{:5.2f} y:{:5.2f}", pos.x, pos.y);
				vig_lo_next();
				vig_checkbox(plr_ent->get_eqp()->infinite_ammo, "Infinite ammo");
			}
		});
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
		}
		if (plr_ent) {
			auto g = pc_ctr->lock();
			pc_ctr->on_event(ev);
		}
	}
	void render(TimeSpan passed)
	{
		std::unique_lock lock(ren_lock);
		if (thr_term) LOG_THROW_X("Game failed");
		if (!pres)
		{
			RenImm::get().draw_text(RenderControl::get_size() /2, "Generating...", -1, true, 4.f);
			
			if (gp_init)
			{
				GamePresenter::init(gp_init->gp);
				pres.reset( GamePresenter::get() );
				gp_init.reset();
				
				lmap->ren_init();
				
				VLOGI("FULL INIT {:.3f} seconds", TimeSpan::since_start().seconds());
			}
		}
		else {
			// set camera
			
			if (plr_ent) 
			{
				const float tar_min = 2.f;
				const float tar_max = 15.f;
				const float per_second = 0.1 / TimeSpan::fps(60).seconds();
				
				auto cam = RenderControl::get().get_world_camera();
				
				const vec2fp pos = plr_ent->get_phy().get_pos();
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
				
				const vec2fp scr = cam->mouse_cast(RenderControl::get_size()) - cam->mouse_cast({}) / 2;
				const vec2fp tar_d = (tar - frm.pos);
				
				if (std::fabs(tar_d.x) > scr.x || std::fabs(tar_d.y) > scr.y) frm.pos = tar;
				else frm.pos = lerp(frm.pos, tar, per_second * passed.seconds());
				
				cam->set_state(frm);
			}
			
			// draw world
			
			RenImm::get().set_context(RenImm::DEFCTX_WORLD);
			GamePresenter::get()->render(passed);
			
			if (ph_debug_draw)
				core->get_phy().world.DrawDebugData();
			
			// draw UI
			
			RenImm::get().set_context(RenImm::DEFCTX_UI);
			
			if (vig_current_menu() == VigMenu::Default)
			{
				if (plr_ent)
				{
					auto eqp = plr_ent->get_eqp();
					if (auto wpn = eqp->wpn_ptr())
					{
						vig_label_a("{}\n", wpn->get_reninfo().name);
						if (auto m = wpn->get_ammo()) {
							if (eqp->infinite_ammo) vig_label("AMMO CHEAT ENABLED\n");
							else vig_label_a("Ammo: {} / {}\n", static_cast<int>(m->cur), static_cast<int>(m->max));
						}
						if (auto m = wpn->get_heat()) vig_progress(m->ok()? "Overheat" : "COOLDOWN", m->value);
						if (auto m = wpn->get_rof())  vig_progress(" Ready ", 1 - m->wait / m->delay);
					}
					else vig_label("No weapon equipped");
				}
				else vig_label("Waiting for respawn...");
				vig_lo_next();
			}
			
			std::optional<vec2fp> plr_p;
			if (plr_ent) plr_p = plr_ent->get_phy().get_pos();
			lmap->draw(passed, plr_p, pc_ctr->get_state().is[PlayerController::A_SHOW_MAP]);
		}
	}
	
	
	
	void spawn_plr()
	{
		if (core->get_ent(plr_eid)) return;
		
		vec2fp plr_pos = {};
		for (auto& p : lvl->get_spawns()) {
			if (p.type == LevelControl::SP_PLAYER) {
				plr_pos = p.pos;
				break;
			}
		}
		
		plr_ent = create_player(plr_pos, pc_ctr);
		plr_ent->get_eqp()->infinite_ammo = true;
		plr_eid = plr_ent->index;
	}
	void init_game()
	{	
		TimeSpan t0 = TimeSpan::since_start();
		
//		std::shared_ptr<LevelTerrain> lt( LevelTerrain::generate({ {260,120}, 3 }) );
		std::shared_ptr<LevelTerrain> lt( LevelTerrain::load_testlvl(3) );
//		lt->test_save();
//		exit(666);
		
		lmap.reset( LevelMap::init(*lt) );
		core.reset( GameCore::create({}) );
		lvl.reset( LevelControl::init(*lt) );
		
		gp_init = {{lt}};
		while (!pres)
		{
			if (thr_term) throw std::runtime_error("init_game() interrupted");
			sleep(TimeSpan::fps(30));
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
				spawn_plr();
			}
			auto dt = TimeSpan::since_start() - t0;
			sleep(core->step_len - dt);
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
