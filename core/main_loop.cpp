#include <future>
#include "client/plr_control.hpp"
#include "client/replay.hpp"
#include "core/hard_paths.hpp"
#include "core/vig.hpp"
#include "game/game_core.hpp"
#include "game/level_gen.hpp"
#include "game/player_mgr.hpp"
#include "game_ctr/game_control.hpp"
#include "game_ctr/game_ui.hpp"
#include "game_objects/spawners.hpp"
#include "render/control.hpp"
#include "render/ren_imm.hpp"
#include "render/ren_text.hpp"
#include "vaslib/vas_file.hpp"
#include "vaslib/vas_misc.hpp"
#include "vaslib/vas_string_utils.hpp"
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
	std::unique_ptr<GameControl> gctr;
	std::unique_ptr<GameUI> gui;
	
	std::string init_greet;
	std::future<std::shared_ptr<LevelTerrain>> async_init; // inits gctr
	std::shared_ptr<PlayerController> pc_ctr; // not used
	
	// init args
	
	bool use_gamepad = false;
	bool use_rndseed = false;
	bool is_superman_init = false;
	bool debug_ai_rect_init = false;
	bool no_ffwd = false;
	std::optional<uint32_t> use_seed;
	
	// init replay
	
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
		else if (arg.is("--no-ffwd")) no_ffwd = true;
		else if (arg.is("--superman")) is_superman_init = true;
		else if (arg.is("--dbg-ai-rect")) debug_ai_rect_init = true;
		
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
		else if (arg.is("--demo-last"))
		{
			replay_init_read = ReplayInit_File{ HARDPATH_USR_PREFIX"last.ratdemo" };
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
		
		//
		
		init_greet = GameUI::generate_greet();
		
		async_init = std::async(std::launch::async,
		[this]
		{
			auto p_gci = std::make_unique<GameControl::InitParams>();
			auto& gci = *p_gci;
			
			if (no_ffwd)
				gci.fastforward_time = {};
			
			if (use_seed) {
				gci.rndg.set_seed(*use_seed);
				VLOGI("Level seed (cmd): {}", *use_seed);
			}
			else if (use_rndseed) {
				uint32_t s = fast_hash32(date_time_str());
				gci.rndg.set_seed(s);
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
				replay_dat.rnd_init = gci.rndg.save();
				replay_dat.fastforward = gci.fastforward_time;
				replay_dat.pmg_superman = is_superman_init;
				replay_dat.pmg_dbg_ai_rect = debug_ai_rect_init;
			}
			
			std::visit(overloaded{
				[](std::monostate&){},
				[&](ReplayInit_File& ps){
					VLOGW("DEMO RECORD - FILE \"{}\"", ps.fn.data());
					gci.replay_wr.reset( ReplayWriter::write_file( std::move(replay_dat), ps.fn.data() ));
				},
				[&](ReplayInit_Net& ps){
					VLOGW("DEMO RECORD - NETWORK");
					gci.replay_wr.reset( ReplayWriter::write_net( std::move(replay_dat), ps.addr.data(), ps.port.data(), ps.is_serv ));
				}}
			, replay_init_write);
			
			std::visit(overloaded{
				[](std::monostate&){},
				[&](ReplayInit_File& ps){
					VLOGW("DEMO PLAYBACK - FILE \"{}\"", ps.fn.data());
					gci.replay_rd.reset( ReplayReader::read_file( replay_dat, ps.fn.data() ));
				},
				[&](ReplayInit_Net& ps){
					VLOGW("DEMO PLAYBACK - NETWORK");
					gci.replay_rd.reset( ReplayReader::read_net( replay_dat, ps.addr.data(), ps.port.data(), ps.is_serv ));
				}}
			, replay_init_read);
			
			if (gci.replay_rd) {
				gci.rndg.load( replay_dat.rnd_init );
				gci.fastforward_time = replay_dat.fastforward;
				is_superman_init = replay_dat.pmg_superman;
				debug_ai_rect_init = replay_dat.pmg_dbg_ai_rect;
			}
			
			// init
			
			std::shared_ptr<LevelTerrain> lt( LevelTerrain::generate({ &gci.rndg, {220,140} }) );
			
			gci.lt = lt;
			gci.pc_ctr = pc_ctr;
			gci.spawner = level_spawn;
			
			gctr.reset( GameControl::create(std::move(p_gci)) );
			return lt;
		});
	}
	void on_current()
	{
		gui->on_enter();
	}
	void on_event(const SDL_Event& ev)
	{
		if (!gui) return;
		
		gui->on_event(ev);
		if (ev.type == SDL_KEYUP)
		{
			int k = ev.key.keysym.scancode;
			if (k == SDL_SCANCODE_F1)
			{
				gui->on_leave();
				MainLoop::create(INIT_HELP);
			}
			else if (k == SDL_SCANCODE_ESCAPE)
			{
				gui->on_leave();
				MainLoop::create(INIT_SETTINGS);
				dynamic_cast<ML_Settings&>(*MainLoop::current).ctr = pc_ctr;
				MainLoop::current->init();
			}
		}
	}
	void render(TimeSpan frame_begin, TimeSpan passed)
	{
		if (async_init.valid())
		{
			if (async_init.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
			{
				std::shared_ptr<LevelTerrain> lt;
				try {lt = async_init.get();}
				catch (std::exception& e) {LOG_THROW("Game init exception - {}", e.what());}
				
				GameUI::InitParams pars;
				pars.ctr = gctr.get();
				pars.pc_ctr = pc_ctr;
				pars.lt = std::move(lt);
				pars.init_greet = std::move(init_greet);
				gui.reset( GameUI::create(std::move(pars)) );
				
				if (!gctr->get_replay_reader()) {
					gctr->get_core().get_pmg().is_superman   = is_superman_init;
					gctr->get_core().get_pmg().debug_ai_rect = debug_ai_rect_init;
				}
				gctr->set_pause(false);
			}
			else draw_text_message(std::string("Generating...\n\n") + init_greet);
		}
		else gui->render(frame_begin, passed);
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
