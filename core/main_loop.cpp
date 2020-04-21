#include <SDL2/SDL_messagebox.h>
#include "client/plr_input.hpp"
#include "client/replay.hpp"
#include "core/hard_paths.hpp"
#include "core/vig.hpp"
#include "game/game_core.hpp"
#include "game/game_mode.hpp"
#include "game/level_gen.hpp"
#include "game/player_mgr.hpp"
#include "game_ctr/game_control.hpp"
#include "game_ctr/game_ui.hpp"
#include "game_objects/spawners.hpp"
#include "game_objects/tutorial.hpp"
#include "render/control.hpp"
#include "render/ren_imm.hpp"
#include "render/ren_text.hpp"
#include "utils/line_cfg.hpp"
#include "vaslib/vas_file.hpp"
#include "vaslib/vas_misc.hpp"
#include "vaslib/vas_string_utils.hpp"
#include "vaslib/vas_log.hpp"
#include "main_loop.hpp"



size_t MainLoop::show_ask_box(std::string text, std::vector<std::string> responses)
{
	std::vector<SDL_MessageBoxButtonData> bs;
	for (auto& str : responses)
	{
		auto& b = bs.emplace_back();
		b = {};
		b.text = str.c_str();
		b.buttonid = bs.size() - 1;
	}
	
	SDL_MessageBoxData d = {};
	d.flags = SDL_MESSAGEBOX_WARNING | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
	d.title = "Confirm";
	d.message = text.c_str();
	d.numbuttons = responses.size();
	d.buttons = bs.data();
	
	int hit = -1;
	if (SDL_ShowMessageBox(&d, &hit))
		VLOGE("SDL_ShowMessageBox failed - {}", SDL_GetError());
	
	return std::max(0, hit);
}
void MainLoop::show_error_msg(std::string text, std::string title)
{
	if (!is_debug_mode) 
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), text.c_str(), nullptr);
}
void MainLoop::show_internal_error(std::string text)
{
	show_error_msg(FMT_FORMAT("{}.\n\nShow log (user/app.log) to developer", text), "Internal error");
}



class ML_MainMenu : public MainLoop
{
public:
	struct Line {
		std::string text;
		std::function<void()> on_press;
		Rect at = {};
	};
	std::vector<Line> lines;
	vec2i size;
	
	ML_MainMenu() {}
	void init()
	{
		static auto start_game = [](std::vector<std::string> args)
		{
			create(INIT_GAME, false);
			
			ArgvParse arg;
			arg.args = std::move(args);
			try {
				while (!arg.ended())
					if (!current->parse_arg(arg))
						THROW_FMTSTR("Unknown command-line option: {}", arg.cur());
			}
			catch (std::exception& e) {
				THROW_FMTSTR("Menu:newgame arg parse failed: {}", e.what());
			}
			
			current->init();
		};
		
		if (!ml_prev)
		{
			lines.push_back({"MAIN MENU\n", {}});
			lines.push_back({"Tutorial", {[]{ start_game({"--tutorial"}); }}});
			lines.push_back({"Default", {[]{ start_game({}); }}});
			lines.push_back({"New game", {[]{ start_game({"--rndseed"}); }}});
			lines.push_back({"Keybinds", {[]{ create(INIT_KEYBIND); }}});
			lines.push_back({"Controls", {[]{ create(INIT_HELP); }}});
			lines.push_back({"Exit", [this]{ delete this; }});
		}
		else
		{
			lines.push_back({"PAUSE MENU\n", {}});
			lines.push_back({"Beware! Game is not saved!\n", {}});
			lines.push_back({"Keybinds", {[]{ create(INIT_KEYBIND); }}});
			lines.push_back({"Controls", {[]{ create(INIT_HELP); }}});
			lines.push_back({"Return to game", [this]{ delete this; }});
			lines.push_back({"Exit to main menu", []{
				while (current) delete current;
				create(INIT_MAINMENU);
			}});
			lines.push_back({"Exit to desktop", []{ while (current) delete current; }});
		}
		
		int n = int(lines.size());
		vigTableLC lc({1, n});
		for (int i=0; i<n; ++i) {
			lc.get({0,i}).str = lines[i].text;
		}
		
		lc.calc();
		size = lc.get_screen_size();
		
		for (int i=0; i<n; ++i) {
			lines[i].at.off = lc.get({0,i}).pos;
			lines[i].at.sz  = lc.get({0,i}).size;
		}
	}
	void on_event(const SDL_Event& ev)
	{
		if (ml_prev && ev.type == SDL_KEYDOWN && 
		    ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
				delete this;
	}
	void render(TimeSpan, TimeSpan)
	{
		vec2i off = (RenderControl::get_size() - size) /2;
		for (auto& l : lines)
		{
			if (!l.on_press) {
				vig_label(l.text, off + l.at.lower(), l.at.size());
				continue;
			}
			if (vig_button(l.text, 0,0,0, off + l.at.lower(), l.at.size())) {
				l.on_press();
				return;
			}
		}
	}
};



class ML_Keybind : public MainLoop
{
public:
	std::vector<PlayerInput::Bind*> binds;
	std::optional<std::pair<int, int>> bix;
	vec2i scroll_pos = {};
	
	std::vector<std::pair<int, int>> conflicts;
	bool conflict_msg = false;
	
	ML_Keybind()
	{
		for (auto& b : PlayerInput::get().binds_ref(PlayerInput::CTX_GAME)) {
			if (!b.hidden)
				binds.push_back(&b);
		}
		update_conflicts();
	}
	void on_event(const SDL_Event& ev)
	{
		if (!bix)
		{
			if (ev.type == SDL_KEYDOWN)
			{
				int k = ev.key.keysym.scancode;
				if (k == SDL_SCANCODE_ESCAPE)
				{
					if (conflicts.empty())
						delete this;
					else
						conflict_msg = !conflict_msg;
				}
				else if (conflict_msg && k == SDL_SCANCODE_Y) {
					delete this;
				}
			}
		}
		else if (ev.type == SDL_KEYDOWN &&
			     ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
		{
			bix.reset();
		}
		else if (binds[bix->first]->ims()[bix->second]->set_from(ev))
		{
			bix.reset();
			update_conflicts();
			
			if (LineCfg(PlayerInput::get().gen_cfg_opts()).write(HARDPATH_KEYBINDS))
				VLOGI("Saved new keybinds");
			else
				VLOGE("Failed to save new keybinds");
		}
	}
	void render(TimeSpan, TimeSpan)
	{
		RenImm::get().draw_rect({{}, RenderControl::get_size(), false}, 0xff);
		if (conflict_msg) {
			draw_text_message("Binding conflict!\n\n"
			                  "Press ESCAPE to resolve   \n"
			                  "Press Y to ignore and exit");
			return;
		}
		
		vig_lo_push_scroll( RenderControl::get_size() - vig_element_decor()*2, scroll_pos );
		
		vig_label("=== KEYBINDS ===\n");
		vig_label("Click on table cell to change value\n");
		vig_label("Place cursor over table cell to see description\n");
		
		if (vig_button("Return to menu")) {
			if (conflicts.empty())
				delete this;
			else
				conflict_msg = true;
			return;
		}
		vig_space_tab();
		if (vig_button("Reset to defaults")) {
			PlayerInput::get().set_defaults();
			std::remove(HARDPATH_KEYBINDS);
			update_conflicts();
		}
		vig_space_line();
		vig_lo_next();
		
		const int i_btype = 1 + PlayerInput::Bind::ims_num;
		vigTableLC lc( vec2i(i_btype + 1, binds.size() + 1) );
		lc.get({0,0}).str = "ACTION";
		lc.get({1,0}).str = "KEY    ";
		lc.get({2,0}).str = "KEY    ";
		lc.get({3,0}).str = "MOUSE  ";
		lc.get({4,0}).str = "TYPE   ";
		
		const char *hd_str[] = {
		    "",
		    "Primary key",
		    "Alternate key",
		    "Mouse button",
		    "Action type"
		};
		
		for (size_t i=0; i<binds.size(); ++i)
		{
			auto& bind = *binds[i];
			lc.get( vec2i(0, i+1) ).str = bind.name;
			
			for (size_t j=0; j<bind.ims().size(); ++j)
				lc.get( vec2i(j+1, i+1) ).str = bind.ims()[j]->name.str;
			
			lc.get( vec2i(i_btype, i+1) ).str = [&]
			{
				switch (bind.type)
				{
				case PlayerInput::BT_TRIGGER:
					return "Action";
					
				case PlayerInput::BT_HELD:
					return "Hold";
					
				case PlayerInput::BT_SWITCH:
					return "Switch";
				}
				return ""; // silence warning
			}();
		}
		
		lc.calc();
		vec2i off = lc.place();
		
		for (int y=0; y < lc.get_size().y; ++y)
		for (int x=0; x < lc.get_size().x; ++x)
		{
			auto& e = lc.get({x,y});
			if (!y || !x)
			{
				vig_label(*e.str, off + e.pos, e.size);
				if (y) vig_tooltip( binds[y-1]->descr, off + e.pos, e.size );
				else vig_tooltip( hd_str[x], off + e.pos, e.size );
			}
			else if (x == i_btype)
			{
				bool pressed = vig_button(*e.str, 0, false, false, off + e.pos, e.size);
				
				auto& bind = *binds[y-1];
				switch (bind.type)
				{
				case PlayerInput::BT_TRIGGER:
					vig_tooltip("Triggered once on press", off + e.pos, e.size);
					break;
					
				case PlayerInput::BT_HELD:
					if (pressed) {
						bind.type = PlayerInput::BT_SWITCH;
					}
					vig_tooltip("Enabled all time while pressed", off + e.pos, e.size);
					break;
					
				case PlayerInput::BT_SWITCH:
					if (pressed) {
						bind.type = PlayerInput::BT_HELD;
						bind.sw_val = false;
					}
					vig_tooltip("Switched once on press", off + e.pos, e.size);
					break;
				}
			}
			else {
				bool act = bix? y == bix->first + 1 && x == bix->second + 1 : false;
				bool err = false;
				
				if (!bix) {
					for (auto& c : conflicts)
						if (c.first == y-1 && c.second == x-1) {
							err = true;
							vig_push_palette();
							vig_CLR(Active) = vig_CLR(Incorrect);
							break;
						}
				}
				
				if (vig_button(*e.str, 0, act || err, false, off + e.pos, e.size) && !bix)
					bix = std::make_pair(y-1, x-1);
				
				if (err)
					vig_pop_palette();
			}
		}
		
		if (bix)
			vig_tooltip("Press new key or button to bind to control.\nPress ESCAPE to keep current bind.", true);
	}
	void update_conflicts()
	{
		conflicts.clear();
		auto& bs = PlayerInput::get().binds_ref(PlayerInput::CTX_GAME);
		
		for (size_t i=0; i<binds.size(); ++i)
		for (size_t j=0; j<bs.size(); ++j)
		{
			if (binds[i] == &bs[j]) continue;
			for (size_t k=0; k<PlayerInput::Bind::ims_num; ++k)
			{
				if (binds[i]->ims()[k]->is_same( *bs[j].ims()[k] ))
					conflicts.emplace_back(i, k);
			}
		}
		
		PlayerInput::get().after_load();
	}
};



class ML_Help : public MainLoop
{
public:
	TextRenderInfo tri;
	vec2i scroll_pos = {};
	
	ML_Help() {
		tri.str_a = R"(
=== CONTROLS LIST ===				=== REPLAY PLAYBACK ===

F1       toggle this screen			1	increase speed x2
ESC      open keybinds menu			2	decrease speed x2
Pause	 pause game					3	reset speed to normal

=== GAME ===						=== TELEPORT MENU ===

See Keybinds menu					LMB	 select teleport
									ESC	 exit menu
=== DEBUG ===

B	toggle far camera			Shift (held) debug mode					H	toggle pause
F4	toggle godmode				F7    toggle debug mode					J	step world once
0	toggle physics debug draw	P     (debug mode) toggle puppet mode	K	toggle free camera
								LMB   (debug mode) select object		L	reset free camera
								RMB   (debug mode) teleport at			Arrows  control free camera
								RMB   (puppet mode) move to
=== APP DEBUG ===

~ + Q   exit immediatly				F2      toggle log
~ + R   reload shaders				F3      toggle fps counter
~ + F   switch fullscreen mode		~ + 1   debug menu: disable
~ + S   reset window to minimal		~ + 2   debug menu: rendering
~ + C   reload config				~ + 3   debug menu: game
~     while held, blocks non-UI input)";
		tri.build();
	}
	void on_event(const SDL_Event& ev) {
		if (ev.type == SDL_KEYDOWN && 
		    !ev.key.repeat && (
			ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE ||
			ev.key.keysym.scancode == SDL_SCANCODE_F1))
				delete this;
	}
	void render(TimeSpan, TimeSpan)
	{
		vig_lo_push_scroll(RenderControl::get_size() - vig_element_decor()*2, scroll_pos);
		vec2i offset, size = tri.size.int_ceil();
		vig_lo_place(offset, size);
		RenImm::get().draw_text(offset, tri, RenImm::White);
		vig_lo_pop();
	}
};



class ML_Game : public MainLoop
{
public:
	std::unique_ptr<GameControl> gctr;
	std::unique_ptr<GameUI> gui;
	bool gctr_inited = false;
	
	std::string init_greet;
	size_t i_cheat = 0;
	
	// init args
	
	bool use_rndseed = false;
	bool no_ffwd = false;
	std::optional<uint32_t> use_seed;
	bool is_tutorial = false;
	bool nodrop = false;
	bool nohunt = false;
	vec2i lvl_size = {220, 140};
	
	bool is_superman_init = false;
	bool debug_ai_rect_init = false;
	bool save_terrain = false;
	
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
	~ML_Game() {if (gui) gui->on_leave();}
	bool parse_arg(ArgvParse& arg)
	{
		if		(arg.is("--rndseed")) use_rndseed = true;
		else if (arg.is("--seed")) use_seed = arg.i32();
		else if (arg.is("--no-ffwd")) no_ffwd = true;
		else if (arg.is("--tutorial")) {
			is_tutorial = true;
			replay_write_default = false;
		}
		else if (arg.is("--nodrop")) nodrop = true;
		else if (arg.is("--nohunt")) nohunt = true;
		
		else if (arg.is("--lvl-size")) {
			lvl_size.x = arg.i32();
			lvl_size.y = arg.i32();
		}
		
		else if (arg.is("--superman")) is_superman_init = true;
		else if (arg.is("--dbg-ai-rect")) debug_ai_rect_init = true;
		else if (arg.is("--save-terr")) save_terrain = true;
		
		else if (arg.is("--no-demo-record")) {
			replay_write_default = false;
		}
		else if (arg.is("--demo-record")) {
			replay_init_write = ReplayInit_File{FMT_FORMAT(HARDPATH_DEMO_TEMPLATE, date_time_fn())};
		}
		else if (arg.is("--last-record")) {
			replay_init_write = ReplayInit_File{HARDPATH_DEMO_LAST};
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
		PlayerInput::get(); // init
		init_greet = GameUI::generate_greet();
		
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
			&& !isok(replay_init_read))
		{
			replay_init_write = MainLoop::startup_date
				? ReplayInit_File{FMT_FORMAT(HARDPATH_DEMO_TEMPLATE, date_time_fn())}
				: ReplayInit_File{HARDPATH_USR_PREFIX"last.ratdemo"};
		}
		
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
			is_superman_init   = replay_dat.pmg_superman;
			debug_ai_rect_init = replay_dat.pmg_dbg_ai_rect;
		}
		
		// init
		
		if (!is_tutorial) {
			gci.disable_drop = nodrop;
			gci.disable_hunters = nohunt;
			
			gci.mode_ctr.reset(GameModeCtr::create());
			gci.terrgen = [&](auto& rnd) {return LevelTerrain::generate({&rnd, lvl_size});};
			gci.spawner = level_spawn;
			
//			gci.terrgen = [](auto&) {return drone_test_terrain();};
//			gci.spawner = drone_test_spawn;
		}
		else {
			gci.mode_ctr.reset(GameModeCtr::create_tutorial());
			gci.terrgen = [](auto&) {return tutorial_terrain();};
			gci.spawner = tutorial_spawn;
		}
		gctr.reset( GameControl::create(std::move(p_gci)) );
		
		GameUI::InitParams pars;
		pars.ctr = gctr.get();
		pars.init_greet = std::move(init_greet);
		pars.allow_cheats = MainLoop::is_debug_mode;
		pars.is_tutorial = is_tutorial;
		gui.reset( GameUI::create(std::move(pars)) );
	}
	void on_current()
	{
		gui->on_enter();
	}
	void on_event(const SDL_Event& ev)
	{
		if (gui)
			gui->on_event(ev);
		
		constexpr std::string_view s("90-88-90");
		if (ev.type == SDL_KEYUP && i_cheat != s.length()) {
			if (ev.key.keysym.sym == s[i_cheat] && (ev.key.keysym.mod & KMOD_SHIFT)) {
				++i_cheat;
				if (i_cheat == s.length()) {
					VLOGI("Cheat sequence entered");
					gui->enable_debug_mode();
				}
			}
			else i_cheat = 0;
		}
	}
	void render(TimeSpan frame_begin, TimeSpan passed)
	{
		if (!gctr_inited && std::holds_alternative<GameControl::CS_Run>(gctr->get_state()))
		{
		    gctr_inited = true;
			if (!gctr->get_replay_reader()) {
				gctr->get_core().get_pmg().is_superman   = is_superman_init;
				gctr->get_core().get_pmg().debug_ai_rect = debug_ai_rect_init;
			}
			
			if (save_terrain)
				gctr->get_terrain()->debug_save("terrain");
			
			gui->on_enter();
		}
		
		gui->render(frame_begin, passed);
		
		PlayerInput::State st;
		{	auto lock = PlayerInput::get().lock();
			st = PlayerInput::get().get_state(PlayerInput::CTX_GAME);
		}
		
		if (st.is[PlayerInput::A_MENU_HELP]) {
			gui->on_leave();
			MainLoop::create(INIT_HELP);
		}
		else if (st.is[PlayerInput::A_MENU_EXIT]) {
			gui->on_leave();
			MainLoop::create(INIT_MAINMENU);
		}
	}
};



MainLoop* MainLoop::current;

void MainLoop::create(InitWhich which, bool do_init)
{
	MainLoop* prev = current;
	
	try {
		switch (which)
		{
		case INIT_DEFAULT:
		case INIT_MAINMENU:
			current = new ML_MainMenu;
			break;
			
		case INIT_DEFAULT_CLI:
		case INIT_GAME:
			current = new ML_Game;
			break;
			
		case INIT_KEYBIND:
			current = new ML_Keybind;
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
	if (do_init)
		current->init();
}
MainLoop::~MainLoop() {
	if (current == this) {
		current = ml_prev;
		if (current) current->on_current();
	}
}
bool MainLoop::parse_arg(ArgvParse&) {return false;}
