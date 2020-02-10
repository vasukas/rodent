#include <atomic>
#include <mutex>
#include <thread>
#include "client/level_map.hpp"
#include "client/plr_control.hpp"
#include "client/presenter.hpp"
#include "client/replay.hpp"
#include "core/hard_paths.hpp"
#include "core/settings.hpp"
#include "core/vig.hpp"
#include "game/game_core.hpp"
#include "game/level_ctr.hpp"
#include "game/level_gen.hpp"
#include "game/player_mgr.hpp"
#include "game_ai/ai_drone.hpp"
#include "game_objects/objs_basic.hpp"
#include "game_objects/player.hpp"
#include "game_objects/spawners.hpp"
#include "render/camera.hpp"
#include "render/control.hpp"
#include "render/postproc.hpp"
#include "render/ren_aal.hpp"
#include "render/ren_imm.hpp"
#include "render/ren_particles.hpp"
#include "render/ren_text.hpp"
#include "utils/noise.hpp"
#include "utils/path_search.hpp"
#include "utils/res_image.hpp"
#include "utils/time_utils.hpp"
#include "vaslib/vas_file.hpp"
#include "vaslib/vas_log.hpp"
#include "vaslib/vas_string_utils.hpp"
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
	void render(TimeSpan, TimeSpan)
	{
		RenImm::get().draw_rect({{}, RenderControl::get_size(), false}, 0xff);
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



class ML_Help : public MainLoop
{
public:
	std::optional<TextRenderInfo> tri;
	void init() {}
	void on_event(const SDL_Event& ev)
	{
		if (ev.type == SDL_KEYUP && (
		        ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE ||
		        ev.key.keysym.scancode == SDL_SCANCODE_F1))
			delete this;
	}
	void render(TimeSpan, TimeSpan)
	{
		if (!tri) {
			tri = TextRenderInfo{};
			tri->str_a = R"(=== CONTROLS LIST ===

F1       toggle this menu
ESC      show keyboard bindings

=== GAME ===

W,A,S,D  movement					G   toggle shield
Space    acceleration				R   toggle laser designator
Mouse    aim						E   interact
LMB      primary fire				M   (held) show map
RMB      alt. fire					1-6             select weapon
Ctrl     track mouse movement		Mousewheel,[,]  change weapon

=== REPLAY PLAYBACK ===

1		increase speed x2
2		decrease speed x2
3		speed = 1

=== DEBUG ===

B		toggle far camera			Shift (held) debug mode
F4		toggle godmode				F7    toggle debug mode
F5		save full world renderer	P     (debug mode) toggle puppet mode
									LMB   (debug mode) select object
0		toggle physics debug draw	RMB   (debug mode) teleport at
F9		toggle AoS debug draw		RMB   (puppet mode) move to
F10		toggle particles rendering

=== APP DEBUG ===

~ + Q   exit immediatly				F2      toggle log
~ + R   reload shaders				~ + 1   debug menu: disable
~ + F   switch fullscreen mode		~ + 2   debug menu: rendering
~ + S   reset window to minimal		~ + 3   debug menu: game
~ + C   reload config)";
			tri->build();
		}
		RenImm::get().draw_text(vec2fp::one(6), *tri, RenImm::White);
	}
};



class ML_Game : public MainLoop
{
public:
	TimeSpan fastforward_time = TimeSpan::seconds(10);
	const TimeSpan fastforward_fullworld = TimeSpan::seconds(5); // how long full world is simulated
	
	std::unique_ptr<GamePresenter> pres;
	std::unique_ptr<LevelControl> lvl;
	std::unique_ptr<AI_Controller> ai_ctr;
	std::unique_ptr<GameCore> core;
	std::shared_ptr<PlayerController> pc_ctr;
	
	std::unique_ptr<ReplayWriter> replay_wr;
	std::unique_ptr<ReplayReader> replay_pb;
	std::optional<float> pb_speed_k;
	
	std::atomic<bool> thr_term = false;
	std::thread thr_serv;
	std::mutex ren_lock;
	
	std::atomic<bool> pause_logic = false; ///< Control
	std::atomic<bool> pause_logic_ok = false; ///< Is really paused
	bool is_ren_paused = false; ///< Is paused be window becoming non-visible
	
	TimeSpan hide_pause_until; ///< Pause stub is not drawn until after that time
	
	//

	struct GP_Init
	{
		GamePresenter::InitParams gp;
	};
	std::optional<GP_Init> gp_init;
	std::atomic<bool> game_fin = false;
	bool is_first_frame = true;
	
	bool use_gamepad = false;
	bool use_rndseed = false;
	std::optional<uint32_t> use_seed;
	
	//
	
	bool ph_debug_draw = false;
	vigAverage dbg_serv_avg;
	RAII_Guard dbg_serv_g;
	std::atomic<std::optional<float>> dbg_serv_avg_last;
	
	double serv_avg_total = 0;
	size_t serv_avg_count = 0;
	size_t serv_overmax_count = 0;
	
	bool dbg_lag_render = 0;
	bool dbg_lag_logic  = 0;
	
	//
	
	bool ui_dbg_mode = false;
	EntityIndex dbg_select;
	
	bool ui_dbg_puppet_switch = false;
	
	//
	
	std::unique_ptr<LevelMap> lmap;
	std::unique_ptr<Texture> crosshair_tex;
	
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
	
	//
	
	struct ReplayInit_File {
		std::string fn;
	};
	struct ReplayInit_Net {
		std::string addr, port;
		bool is_serv;
	};
	using ReplayInit = std::variant<std::monostate, ReplayInit_File, ReplayInit_Net>;
	
	ReplayInit replay_init_write;
	ReplayInit replay_init_read;
	bool replay_write_default = true;
	
	static bool isok(ReplayInit& v) {return !std::holds_alternative<std::monostate>(v);}
	
	
	
	ML_Game() = default;
	bool parse_arg(ArgvParse& arg)
	{
		if		(arg.is("--gpad-on"))  use_gamepad = true;
		else if (arg.is("--gpad-off")) use_gamepad = false;
		else if (arg.is("--cheats")) PlayerController::allow_cheats = true;
		else if (arg.is("--rndseed")) use_rndseed = true;
		else if (arg.is("--seed")) use_seed = arg.i32();
		else if (arg.is("--no-ffwd")) fastforward_time = {};
		else if (arg.is("--superman")) PlayerEntity::is_superman = true;
		
		else if (arg.is("--no-demo-record")) {
			replay_write_default = false;
		}
		else if (arg.is("--demo-record")) {
			replay_init_write = ReplayInit_File{ FMT_FORMAT(HARDPATH_DEMO_TEMPLATE, date_time_fn()) };
		}
		else if (arg.is("--demo-write"))
		{
			auto s = arg.str();
			if (!ends_with(s, ".ratdemo")) s += ".ratdemo";
			replay_init_write = ReplayInit_File{ std::move(s) };
		}
		else if (arg.is("--demo-play"))
		{
			auto s = arg.str();
			if (get_file_ext(s).empty()) s += ".ratdemo";
			replay_init_read = ReplayInit_File{ std::move(s) };
		}
		else if (arg.is("--demo-net"))
		{
			auto p1 = arg.str();
			auto p2 = arg.str();
			auto p3 = arg.flag();
			replay_init_write = ReplayInit_Net{ std::move(p1), std::move(p2), p3 };
		}
		else if (arg.is("--demo-net-play"))
		{
			auto p1 = arg.str();
			auto p2 = arg.str();
			auto p3 = arg.flag();
			replay_init_read = ReplayInit_Net{ std::move(p1), std::move(p2), p3 };
		}
		
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
				vig_checkbox(GameCore::get().dbg_ai_see_plr, "AI see player");
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
					upd_cheats |= vig_checkbox(PlayerEntity::is_superman, "Superman (requires respawn)");
					if (upd_cheats) core->get_pmg().update_cheats();
				}
				else vig_label("Cheats disabled");
				
				vig_lo_next();
				vig_checkbox(dbg_lag_render, "Lag render");
				vig_checkbox(dbg_lag_logic,  "Lag core");
				vig_lo_next();
			}
		});
		
		// init
		
		crosshair_tex.reset(Texture::load(HARDPATH_CROSSHAIR_IMG));
		
		thr_serv = std::thread([this]{thr_func();});
	}
	~ML_Game()
	{
		thr_term = true;
		if (thr_serv.joinable())
			thr_serv.join();
		
		VLOGI("Average logic frame length: {} ms, {} samples", serv_avg_total, serv_avg_count);
		VLOGI("Logic frame length > sleep time: {} samples", serv_overmax_count);
	}
	void on_current()
	{
		pause_logic = false;
		RenAAL::get().draw_grid = true;
		
		hide_pause_until = TimeSpan::since_start() + TimeSpan::seconds(0.5);
//		Postproc::get().tint_reset();
//		Postproc::get().tint_seq({}, FColor(0.5, 0.5, 0.5, 0.5));
//		Postproc::get().tint_default(TimeSpan::seconds(0.2));
	}
	void on_event(const SDL_Event& ev)
	{
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
				pause_logic = false;
				is_ren_paused = false;
				return;
			}
			
			auto mod_pb_speed = [&](auto f) {
				if (!replay_pb) return;
				float v = f(pb_speed_k.value_or(1));
				if (aequ(v, 1, 0.01)) pb_speed_k = {};
				else pb_speed_k = v;
			};
			
			if (k == SDL_SCANCODE_F1)
			{
				pause_logic = true;
				RenAAL::get().draw_grid = false;
				
				MainLoop::create(INIT_HELP);
			}
			else if (k == SDL_SCANCODE_ESCAPE)
			{
				pause_logic = true;
				RenAAL::get().draw_grid = false;
				
				MainLoop::create(INIT_SETTINGS);
				dynamic_cast<ML_Settings&>(*MainLoop::current).ctr = pc_ctr;
				MainLoop::current->init();
			}
			else if (k == SDL_SCANCODE_1) mod_pb_speed([](float v){return v/2;});
			else if (k == SDL_SCANCODE_2) mod_pb_speed([](float v){return v*2;});
			else if (k == SDL_SCANCODE_3) mod_pb_speed([](float){return 1;});
			else if (!is_first_frame && PlayerController::allow_cheats)
			{
				if (k == SDL_SCANCODE_0) ph_debug_draw = !ph_debug_draw;
				else if (k == SDL_SCANCODE_B) is_far_cam = !is_far_cam;
				else if (k == SDL_SCANCODE_F4)
				{
					auto& pm = core->get_pmg();
					pm.cheat_godmode = !pm.cheat_godmode;
					pm.update_cheats();
				}
				else if (k == SDL_SCANCODE_F5) save_automap("AUTOMAP.png");
				else if (k == SDL_SCANCODE_F7) {
					ui_dbg_mode = !ui_dbg_mode;
					ui_dbg_puppet_switch = false;
				}
				else if (k == SDL_SCANCODE_F9) AI_Controller::get().show_aos_debug = !AI_Controller::get().show_aos_debug;
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
			if (is_ren_paused) {
				pause_logic = false;
				is_ren_paused = false;
				return;
			}
		}
		
		if (!pause_logic && !ui_dbg_mode) {
			auto g = pc_ctr->lock();
			pc_ctr->on_event(ev);
		}
	}
	void render(TimeSpan frame_begin, TimeSpan passed)
	{
		if (dbg_lag_render)
			sleep(TimeSpan::ms(rnd_stat().int_range(20, 35)));
		
		if (pb_speed_k)
			passed *= *pb_speed_k;
		
		std::unique_lock lock(ren_lock);
		if (thr_term) throw std::runtime_error("Game failed");
		
		if (auto t = dbg_serv_avg_last.exchange({}))
			dbg_serv_avg.add(*t);
		
		if (!pres)
		{
			static std::string gen_msg;
			if (gen_msg.empty())
			{
				auto& rnd = rnd_stat();
				rnd.set_seed( fast_hash32(date_time_str()) );
				
				std::vector<std::string> vs;
				if (rnd.range_n() < 0.3) {}
				else if (rnd.range_n() < 0.8) {
					vs.emplace_back("Incrementing headcrabs...");
					vs.emplace_back("Reversing linked lists...");
					vs.emplace_back("Summoning CPU spirits...");
					vs.emplace_back("Converting walls to zombies...");
					vs.emplace_back("Uploading browser history...");
					vs.emplace_back("SPAM SPAM SPAM SPAM\nSPAM SPAM SPAM SPAM\nLovely spam!\nWonderful spam!");
					vs.emplace_back("Cake isn't implemented yet");
				} else {
					vs.emplace_back("///\\.oOOo./\\\\\\");
					vs.emplace_back("(;;)");
					vs.emplace_back("()_.._()");
				}
				
				gen_msg = "Generating...";
				if (!vs.empty()) {
					gen_msg += "\n\n";
					gen_msg += rnd.random_el(vs);
				}
			}
			draw_text_message(gen_msg);
			
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
				
				RenAAL::get().draw_grid = true;
				Postproc::get().tint_seq({}, FColor(0,0,0,0));
			}
		}
		else if (game_fin)
		{
			RenImm::get().set_context(RenImm::DEFCTX_WORLD);
			GamePresenter::get()->render(frame_begin, {});
			//
			RenImm::get().set_context(RenImm::DEFCTX_UI);
			RenImm::get().draw_rect({{}, RenderControl::get_size(), false}, 0xa0);
			
			winrars.resize(40);
			for (auto& w : winrars) w.draw();
			draw_text_message("Game completed.\n\nA WINRAR IS YOU.");
		}
		else if (!RenderControl::get().is_visible())
		{
			pause_logic = true;
			is_ren_paused = true;
		}
		else
		{
			if (is_first_frame && core->get().get_step_time() > fastforward_time)
			{
				VLOGI("First game frame rendered at {:.3f} seconds", TimeSpan::since_start().seconds());
				is_first_frame = false;
			}
			
			if (SDL_GetKeyboardFocus() != RenderControl::get().get_wnd())
			{
				pause_logic = true;
				is_ren_paused = true;
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
				
				const vec2fp pos = GamePresenter::get()->playback_hack ? ent->get_pos() : ent->get_ren()->get_pos().pos;
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
			GamePresenter::get()->render( frame_begin, pause_logic ? TimeSpan{} : passed );
			
			if (ph_debug_draw)
				core->get_phy().world.DrawDebugData();
			
			// draw pause stub
			
			if (core->get().get_step_time() < fastforward_time)
			{
				RenImm::get().set_context(RenImm::DEFCTX_UI);
				RenImm::get().draw_rect({{}, RenderControl::get_size(), false}, 0xff);
				draw_text_message("Forwarding...");
				return;
			}
			if (pause_logic || pause_logic_ok)
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
			
			if (vig_current_menu() == VigMenu::Default)
			{
				vec2i mpos;
				if (replay_pb) mpos = RenderControl::get().get_world_camera().direct_cast( pc_ctr->get_state().tar_pos );
				else SDL_GetMouseState(&mpos.x, &mpos.y);
				core->get_pmg().render(passed, mpos);
			}
			
			std::optional<vec2fp> plr_p;
			if (auto ent = core->get_pmg().get_ent()) plr_p = ent->get_phy().get_pos();
			lmap->draw(passed, plr_p, pc_ctr->get_state().is[PlayerController::A_SHOW_MAP]);
			
			if (replay_pb || (pc_ctr->get_gpad() && pc_ctr->get_gpad()->get_gpad_state() == Gamepad::STATE_OK))
			{
				float z = std::max(10., RenderControl::get_size().minmax().y * 0.03);
				uint32_t clr = 0x00ff20c0;
				
				vec2fp p = pc_ctr->get_state().tar_pos;
				p = RenderControl::get().get_world_camera().direct_cast(p);
				RenImm::get().draw_image(Rectfp::from_center(p, vec2fp::one(z/2)), crosshair_tex.get(), clr);
			}
			
			if (ui_dbg_mode)
			{
				auto plr_ent = static_cast<PlayerEntity*>( core->get_pmg().get_ent() );
				
				bool l_but = (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK);
				bool r_but = (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_RMASK);
				
				vig_label("\n\nDEBUG UI MODE\n");
				if (ui_dbg_puppet_switch) vig_label("PUPPET MODE\n");
				
				// player teleport
				if (plr_ent && r_but && !ui_dbg_puppet_switch)
				{
					const auto p = pc_ctr->get_state().tar_pos;
					const vec2i gp = LevelControl::get().to_cell_coord(p);
					if (Rect({1,1}, LevelControl::get().get_size() - vec2i::one(2), false).contains_le( gp ))
						plr_ent->get_phobj().body->SetTransform( conv(p), 0 );
					
					if (replay_wr)
						replay_wr->add_event(Replay_DebugTeleport{ p });
				}
				
				// object info + AI puppet
				if (auto ent = GameCore::get().valid_ent(dbg_select))
				{
					std::string s;
					s += FMT_FORMAT("EID: {}\n", ent->index.to_int());
					
					if (AI_Drone* d = ent->get_ai_drone()) s += d->get_dbg_state();
					else s += "NO DEBUG STATS";
					draw_text_hud({0, RenderControl::get_size().y / 2.f}, s);
					
					auto& cam = RenderControl::get().get_world_camera();
					auto pos  = cam.direct_cast( ent->get_pos() );
					auto size = cam.direct_cast( ent->get_pos() + vec2fp::one(ent->get_phy().get_radius()) );
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
					
					if (lookat) dbg_select = lookat->index;
					else dbg_select = {};
				}
			}
		}
	}
	
	
	
	void init_game()
	{	
		RandomGen lt_rnd;
		if (use_seed) {
			lt_rnd.set_seed(*use_seed);
			VLOGI("Level seed (cmd): {}", *use_seed);
		}
		else if (use_rndseed) {
			uint32_t s = fast_hash32(date_time_str());
			lt_rnd.set_seed(s);
			VLOGI("Level seed (random): {}", s);
		}
		else VLOGI("Level seed: default");
		
		// setup demo record/playback
		
		if (replay_write_default
		    && !isok(replay_init_write)
		    && !isok(replay_init_read)
		    )
			replay_init_write = ReplayInit_File{HARDPATH_USR_PREFIX"last.ratdemo"};
		
		ReplayInitData replay_dat;
		if (isok(replay_init_write))
		{
			replay_dat.rnd_init = lt_rnd.save();
			replay_dat.fastforward = fastforward_time;
		}
		
		std::visit(overloaded{
			[](std::monostate&){},
			[&](ReplayInit_File& ps){
				VLOGW("DEMO RECORD - FILE \"{}\"", ps.fn.data());
				replay_wr.reset( ReplayWriter::write_file( std::move(replay_dat), ps.fn.data() ));
			},
			[&](ReplayInit_Net& ps){
				VLOGW("DEMO RECORD - NETWORK");
				replay_wr.reset( ReplayWriter::write_net( std::move(replay_dat), ps.addr.data(), ps.port.data(), ps.is_serv ));
			}}
		, replay_init_write);
		
		std::visit(overloaded{
			[](std::monostate&){},
			[&](ReplayInit_File& ps){
				VLOGW("DEMO PLAYBACK - FILE \"{}\"", ps.fn.data());
				replay_pb.reset( ReplayReader::read_file( replay_dat, ps.fn.data() ));
			},
			[&](ReplayInit_Net& ps){
				VLOGW("DEMO PLAYBACK - NETWORK");
				replay_pb.reset( ReplayReader::read_net( replay_dat, ps.addr.data(), ps.port.data(), ps.is_serv ));
			}}
		, replay_init_read);
		
		if (replay_pb) {
			lt_rnd.load( replay_dat.rnd_init );
			fastforward_time = replay_dat.fastforward;
		}
		
		//
		
		TimeSpan t0 = TimeSpan::since_start();
		
		// 260,120 180,100
		std::shared_ptr<LevelTerrain> lt( LevelTerrain::generate({ &lt_rnd, {220,140}, 3 }) );
//#warning Debug exit
//		lt->test_save(); throw std::logic_error("Debug exit");
		
		GameCore::InitParams gci;
		gci.pmg.reset( PlayerManager::create(pc_ctr) );
		
		ai_ctr.reset( AI_Controller::init() );
		lmap.reset( LevelMap::init(*lt) );
		
		core.reset( GameCore::create( std::move(gci) ) );
		core->spawn_drop = AppSettings::get().spawn_drop;
		lvl.reset( LevelControl::init(*lt) );
		
		gp_init = {{lt}};
		while (!pres)
		{
			if (thr_term) throw std::runtime_error("init_game() interrupted");
			sleep(TimeSpan::fps(30));
		}
		
		new EWall(lt->ls_wall);
		level_spawn(*lt);
		lvl->fin_init(*lt);
		
		VLOGI("init_game() finished in {:.3f} seconds", (TimeSpan::since_start() - t0).seconds());
		
		if (fastforward_time.is_positive())
		{
			TimeSpan t1 = TimeSpan::since_start();
	
			core->get_pmg().fastforward = true;
//			lvl->get_aps().set_forced_sync(true);
			
			TimeSpan ren_acc;
			while (core->get_step_time() < fastforward_time)
			{
				auto t0 = TimeSpan::since_start();
				
				std::unique_lock lock(ren_lock);
				core->step(t0);
				ai_ctr->step();
				
				if (core->get_step_time() > fastforward_fullworld)
					core->get_pmg().fastforward = false;
				
				auto dt = TimeSpan::since_start() - t0;
				ren_acc += dt;
				if (ren_acc > TimeSpan::fps(5))
				{
					ren_acc = {};
					sleep(TimeSpan::ms(5)); // should yield
				}
			}
			
			core->get_pmg().fastforward = false;
//			lvl->get_aps().set_forced_sync(false);
			
			VLOGI("init_game() fastforwarded {:.3f} seconds in {:.3f}",
				  fastforward_time.seconds(), (TimeSpan::since_start() - t1).seconds());
		}
	}
	
	void thr_func()
	try
	{
		init_game();
		VLOGI("Game initialized");
		
		while (!thr_term)
		{
			auto t0 = TimeSpan::since_start();
			std::optional<float> sleep_time_k;
			
			pause_logic_ok = pause_logic.load();
			if (!pause_logic)
			{
				std::unique_lock lock(ren_lock);
				auto ctr_lock = pc_ctr->lock();
				
				if (!replay_pb) pc_ctr->update();
				else {
					auto ret = replay_pb->update_server(*pc_ctr);
					if (auto r = std::get_if<ReplayReader::RET_OK>(&ret))
					{
						sleep_time_k = r->pb_speed;
						for (auto& e : r->evs)
						{
							std::visit([&](Replay_DebugTeleport& e) {
								auto plr_ent = static_cast<PlayerEntity*>( core->get_pmg().get_ent() );
								plr_ent->get_phobj().body->SetTransform( conv(e.target), 0 );
							}, e);
							
							if (replay_wr)
								replay_wr->add_event(e);
						}
					}
					else if (std::holds_alternative<ReplayReader::RET_WAIT>(ret)) {
						sleep(core->step_len);
						continue;
					}
					else if (std::holds_alternative<ReplayReader::RET_END>(ret)) {
						VLOGW("DEMO PLAYBACK FINISHED");
						thr_term = true;
						return;
					}
				}
				if (replay_wr)
					replay_wr->update_client(*pc_ctr);

				if (!sleep_time_k && pb_speed_k) sleep_time_k = *pb_speed_k;
				GamePresenter::get()->playback_hack = !!sleep_time_k;
				
				core->step(t0);
				ai_ctr->step();
				
				if (dbg_lag_logic)
					sleep(TimeSpan::ms(rnd_stat().int_range(35, 50)));
			}
			else {
				auto ctr_lock = pc_ctr->lock();
				pc_ctr->update();
			}
			
			if (GameCore::get().get_pmg().is_game_finished())
			{
				game_fin = true;
				break;
			}
			
			auto dt = TimeSpan::since_start() - t0;
			if (dt > core->step_len) {
				VLOGD("Server lag: {:.3f} seconds on step {}", dt.seconds(), serv_avg_count);
				++serv_overmax_count;
			}
			dbg_serv_avg_last = dt.seconds();
			if (!pause_logic_ok) {
				++serv_avg_count;
				serv_avg_total += (dt.seconds() * 1000 - serv_avg_total) / serv_avg_count;
			}
			
			TimeSpan t_sleep = core->step_len;
			if (sleep_time_k) t_sleep *= *sleep_time_k;
			sleep(t_sleep - dt); // precise_sleep causes more stutter on Linux
		}
	}
	catch (std::exception& e) {
		thr_term = true;
		VLOGE("Game failed: {}", e.what());
	}
	
	
	
	void save_automap(const char *filename)
	{
		const bool show_passable = true;
		
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
		
		if (show_passable)
		{
			auto& lc = LevelControl::get();
			vec2i sz = lc.get_size();
			
			for (int y=0; y < sz.y; ++y)
			for (int x=0; x < sz.x; ++x)
			{
				auto& c = lc.cref({x,y});
				if (!c.is_wall) {
					auto r = Rectfp::from_center(lc.to_center_coord({x,y}), vec2fp::one(lc.cell_size /2));
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
			
		case INIT_HELP:
			current = new ML_Help;
			break;
		}
	}
	catch (std::exception& e) {
		THROW_FMTSTR("MainLoop::create() failed: {}", e.what());
	}
	
	current->ml_prev = prev;
}
MainLoop::~MainLoop() {
	if (current == this) {
		current = ml_prev;
		if (current) current->on_current();
	}
}
bool MainLoop::parse_arg(ArgvParse&) {return false;}
