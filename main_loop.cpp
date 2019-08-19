#include <mutex>
#include <thread>
#include "client/presenter.hpp"
#include "core/plr_control.hpp"
#include "core/vig.hpp"
#include "game/game_core.hpp"
#include "game/level_ctr.hpp"
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
		
		thr_serv = std::thread([this] {thr_func();});
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
/*
			if (auto ent = get_plr())
			{
				const float tar_thr = 8.f; // distance after which targeting enables
				const float tar_dist = 15.f; // targeting camera distance
				const float max_dist = 15.f; // max dist of player from camera
				const float corr_thr = 3.f; // correction distance
				const float corr_angle = deg_to_rad(20); // max frame deviation
				const float zero_thr = 0.5f; // stops animation
				
				auto cam = RenderControl::get().get_world_camera();
				const vec2fp scr = cam->mouse_cast(RenderControl::get_size() /2) - cam->mouse_cast({});
				
				auto frm = cam->get_state();
				const vec2fp pos = ent->get_phy().get_pos();
				
				auto& pctr = *pc_ctr;
				vec2fp tar = pctr.is_enabled( PlayerController::A_CAM_FOLLOW )? pctr.get_tar_pos() : pos;
				
				vec2fp tar_d = tar - pos;
				if (tar_d.len() < tar_thr) tar = pos;
				else tar = pos + tar_d.get_norm() * tar_dist;
				
				vec2fp edir = frm.pos - pos;
				if (edir.len() > max_dist)
					tar = pos + edir.get_norm() * corr_thr;
				
				vec2fp corr = tar - frm.pos;
				if (std::fabs(corr.x) > scr.x ||
				    std::fabs(corr.y) > scr.y)
				{
					frm.pos = tar;
					cam->set_state(frm);
					cam->reset_frames();
				}
				else if (corr.len() > corr_thr)
				{
					frm.pos = tar;
					frm.len = TimeSpan::seconds(0.3);
					cam->reset_frames();
					cam->add_frame(frm);
				}
				else if (corr.len() < zero_thr) cam->reset_frames();
				else {
					vec2fp old_corr = cam->last_frame().pos - frm.pos;
					if (wrap_angle_2 (old_corr.angle() - corr.angle()) > corr_angle)
						cam->reset_frames();
				}
			}
*/
			
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
					if (auto m = wpn->get_ammo()) vig_label_a("Ammo: {} / {}\n", (int) m->cur, (int) m->max);
					if (auto m = wpn->get_heat())
					{
						double val = m->value;
						vig_slider(m->ok()? "         " : "COOLDOWN", val);
					}
					if (auto m = wpn->get_rof())
					{
						double val = 1 - m->wait / m->delay;
						vig_slider("R", val);
					}
				}
				else vig_label("No weapon equipped");
			}
		}
	}
	
	
	
	Entity* get_plr()
	{
		if (pc_ent) {
			if (auto e = core->get_ent(pc_ent)) return e;
			pc_ent = 0;
		}
		return nullptr;
	}
	void init_game()
	{	
		core.reset( GameCore::create({}) );
		
		std::vector<std::vector<vec2fp>> ls_grid;
		std::vector<std::vector<vec2fp>> ls_level;
		
		float hsize = 15.f;
		{	
			ls_level = {{{-hsize,-hsize}, {hsize,-hsize}, {hsize,hsize}, {-hsize,hsize}}};
			ls_level.front().push_back( ls_level.front().front() );
			
			for (float i = 2; i < hsize; i += 2)
			{
				ls_grid.emplace_back() = {{-hsize,  i}, {hsize,  i}};
				ls_grid.emplace_back() = {{-hsize, -i}, {hsize, -i}};
				ls_grid.emplace_back() = {{ i, -hsize}, { i, hsize}};
				ls_grid.emplace_back() = {{-i, -hsize}, {-i, hsize}};
			}
			ls_grid.emplace_back() = {{-hsize, 0}, {hsize, 0}};
			ls_grid.emplace_back() = {{0, -hsize}, {0, hsize}};
		}
		
		gp_init = GamePresenter::InitParams{ ls_level, std::move(ls_grid) };
		while (!pres)
		{
			if (thr_term) throw std::runtime_error("init_game() interrupted");
			sleep(TimeSpan::fps(30));
		}
		
		new EWall(ls_level);
		pc_ent = create_player({}, pc_ctr)->index;
		
		for (int i=0; i<25; ++i)
		{
			float rn = hsize - 1;
			vec2fp pos(rnd_range(-rn, rn), rnd_range(-rn, rn));
			(new EPhyBox(pos))->dbg_name = "Box";
		}
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
				GamePresenter::get()->sync();
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
void MainLoop::init( InitWhich which )
{
	if (which == INIT_DEFAULT)
		which = INIT_GAME;
	
	try {
		if		(which == INIT_RENTEST) current = new ML_Rentest;
		else if (which == INIT_GAME)    current = new ML_Game;
	}
	catch (std::exception& e) {
		VLOGE("MainLoop::init() failed: {}", e.what());
	}
}
MainLoop::~MainLoop() {
	if (current == this) current = nullptr;
}
bool MainLoop::parse_arg(ArgvParse&) {return false;}
