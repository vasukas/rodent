#include <future>
#include <mutex>
#include <thread>
#include "client/presenter.hpp"
#include "core/plr_control.hpp"
#include "core/vig.hpp"
#include "game/game_core.hpp"
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



class ML_Rentest : public MainLoop
{
public:
	void init()
	{
	}
	void on_event(SDL_Event& ev)
	{
		if (ev.type == SDL_KEYDOWN)
		{
			auto& ks = ev.key.keysym;
			if (ks.scancode == SDL_SCANCODE_1)
			{
				ParticleGroupStd ptg;
				ptg.px_radius = 4.f; // default
				ptg.count = 1000;
				ptg.radius = 60;
				ptg.colors_range[0] = 192; ptg.colors_range[3] = 255;
				ptg.colors_range[1] = 192; ptg.colors_range[4] = 255;
				ptg.colors_range[2] = 192; ptg.colors_range[5] = 255;
				ptg.alpha = 255;
				ptg.speed_min = 60; ptg.speed_max = 200; // 200, 600
				ptg.TTL.set_ms(2000), ptg.FT.set_ms(1000);
				ptg.TTL_max = ptg.TTL + TimeSpan::ms(500), ptg.FT_max = ptg.FT + TimeSpan::ms(1000);
				ptg.draw({});
			}
		}
	}
	void render(TimeSpan passed)
	{
		static float r = 0;
		RenAAL::get().draw_line({0, 0}, vec2fp(400, 0).get_rotated(r), 0x40ff40ff, 5.f, 60.f, 2.f);
		r += M_PI * 0.5 * passed.seconds();
		
		RenAAL::get().draw_line({-400,  200}, {-220, -200}, 0x4040ffff, 5.f, 60.f);
		RenAAL::get().draw_line({-400, -200}, {-220,  200}, 0xff0000ff, 5.f, 60.f);
		
		RenImm::get().set_context(RenImm::DEFCTX_WORLD);
		RenImm::get().draw_frame({-200, -200, 400, 400}, 0xff0000ff, 3);
		
		RenAAL::get().draw_line({-200, -200}, {200, 200}, 0x00ff80ff, 5.f, 12.f);
		RenAAL::get().draw_chain({{-200, -200}, {50, 50}, {200, -50}}, true, 0x80ff80ff, 8.f, 3.f);
		RenAAL::get().draw_line({-300, 0}, {-300, 0}, 0xffc080ff, 8.f, 20.f);
		
		RenAAL::get().draw_line({250, -200}, {250, 200}, 0xffc080ff, 8.f, 8.f);
		RenAAL::get().draw_line({325, -200}, {325, 200}, 0xffc080ff, 16.f, 3.f);
		RenAAL::get().draw_line({400, -200}, {400, 200}, 0xffc080ff, 5.f, 30.f);
		
		RenAAL::get().draw_line({-440,  200}, {-260, -200}, 0x6060ffff, 5.f, 60.f, 1.7f);
		RenAAL::get().draw_line({-440, -200}, {-260,  200}, 0xff2020ff, 5.f, 60.f, 1.7f);
	}
};



class ML_Game : public MainLoop
{
public:
	std::unique_ptr<GamePresenter> pres;
	std::unique_ptr<GameCore> core;
	std::shared_ptr<PlayerController> pc_ctr;
	
	bool thr_term = false;
	std::thread thr_serv;
	std::mutex ren_lock;

	std::optional<GamePresenter::InitParams> gp_init;
	EntityIndex pc_ent = 0; // player	
	bool ph_debug_draw = false;

	
	
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
			if (k == SDL_SCANCODE_0) ph_debug_draw = !ph_debug_draw;
			
			else if (k == SDL_SCANCODE_B)
			{
				auto cam = RenderControl::get().get_world_camera();
				Camera::Frame cf = cam->get_state();
				cf.mag = 18.f;
				cam->set_state(cf);
			}
			else if (k == SDL_SCANCODE_N)
			{
				auto cam = RenderControl::get().get_world_camera();
				Camera::Frame cf = cam->get_state();
				cf.mag /= 2;
				cam->set_state(cf);
			}
			else if (k == SDL_SCANCODE_M)
			{
				auto cam = RenderControl::get().get_world_camera();
				Camera::Frame cf = cam->get_state();
				cf.mag *= 2;
				cam->set_state(cf);
			}
		}
		if (get_plr()) {
			auto g = pc_ctr->lock();
			pc_ctr->on_event(ev);
		}
	}
	void render(TimeSpan passed)
	{
		std::unique_lock lock(ren_lock);
		if (thr_term) LOG_THROW_X("Game failed");
		if (!pres) {
			RenImm::get().draw_text(RenderControl::get_size() /2, "Loading...", -1, true, 4.f);
			
			if (gp_init)
			{
				GamePresenter::init(*gp_init);
				pres.reset( GamePresenter::get() );
				gp_init.reset();
			}
		}
		else {
			if (auto ent = get_plr())
			{
				const float tar_min = 2.f;
				const float tar_max = 15.f;
				const float per_second = 0.1 / TimeSpan::fps(60).seconds();
				
				auto cam = RenderControl::get().get_world_camera();
				
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
				
				const vec2fp scr = cam->mouse_cast(RenderControl::get_size()) - cam->mouse_cast({});
				const vec2fp tar_d = (tar - frm.pos);
				
				if (std::fabs(tar_d.x) > scr.x || std::fabs(tar_d.y) > scr.y) frm.pos = tar;
				else frm.pos = lerp(frm.pos, tar, per_second * passed.seconds());
				
				cam->set_state(frm);
			}
			
			RenImm::get().set_context(RenImm::DEFCTX_WORLD);
			GamePresenter::get()->render(passed);
			
			if (ph_debug_draw)
				core->get_phy().world.DrawDebugData();
			
			RenImm::get().set_context(RenImm::DEFCTX_UI);
			
			if (auto ent = get_plr())
			{
				if (auto wpn = ent->get_eqp()->wpn_ptr())
				{
					vig_label_a("{}\n", wpn->get_reninfo().name);
					if (auto m = wpn->get_ammo()) {
						if (ent->get_eqp()->infinite_ammo) vig_label("AMMO CHEAT ENABLED\n");
						else vig_label_a("Ammo: {} / {}\n", static_cast<int>(m->cur), static_cast<int>(m->max));
					}
					if (auto m = wpn->get_heat()) vig_progress(m->ok()? "Overheat" : "COOLDOWN", m->value);
					if (auto m = wpn->get_rof())  vig_progress(" Ready ", 1 - m->wait / m->delay);
				}
				else vig_label("No weapon equipped");
			}
		}
	}
	
	
	
	Entity* get_plr()
	{
		return core? core->valid_ent(pc_ent) : nullptr;
	}
	void init_game()
	{	
		TimeSpan t0 = TimeSpan::since_start();
		
		std::shared_ptr<LevelTerrain> lt( LevelTerrain::generate({ {200,80}, 3 }) );
		core.reset( GameCore::create({}) );
		
		gp_init = GamePresenter::InitParams{lt};
		while (!pres)
		{
			if (thr_term) throw std::runtime_error("init_game() interrupted");
			sleep(TimeSpan::fps(30));
		}
		
		auto& r0 = lt->rooms.front().area;
		vec2fp plr_pos = (r0.off + r0.sz /2) * lt->cell_size;
		
		new EWall(lt->ls_wall);
		pc_ent = create_player(plr_pos, pc_ctr)->index;
		core->get_ent(pc_ent)->get_eqp()->infinite_ammo = true;
		
		VLOGI("init_game() finished in: {:.3f} seconds", (TimeSpan::since_start() - t0).seconds());
	}
	void thr_func() try
	{
		init_game();
		VLOGI("Game initialized");
		
		while (!thr_term)
		{
			auto t0 = TimeSpan::since_start();
			{
				std::unique_lock lock(ren_lock);
				core->step();
			}
			sleep(core->step_len - (TimeSpan::since_start() - t0));
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
	if (which == INIT_DEFAULT)
		which = INIT_GAME;
	
	MainLoop* prev = current;
	try {
		if		(which == INIT_RENTEST) current = new ML_Rentest;
		else if (which == INIT_GAME)    current = new ML_Game;
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
