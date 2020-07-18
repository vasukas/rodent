#include <filesystem>
#include <future>
#include <SDL2/SDL_messagebox.h>
#include "client/plr_input.hpp"
#include "client/replay.hpp"
#include "client/sounds.hpp"
#include "core/hard_paths.hpp"
#include "core/settings.hpp"
#include "core/vig.hpp"
#include "game/game_core.hpp"
#include "game/game_mode.hpp"
#include "game/level_gen.hpp"
#include "game/player_mgr.hpp"
#include "game_ctr/game_control.hpp"
#include "game_ctr/game_ui.hpp"
#include "game_objects/spawners.hpp"
#include "game_objects/tutorial.hpp"
#include "render/camera.hpp"
#include "render/control.hpp"
#include "render/postproc.hpp"
#include "render/ren_aal.hpp"
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
	show_error_msg(FMT_FORMAT("{}.\nSee log for details.", text), "Internal error");
}



static bool has_watched_intro() {
	return readfile(HARDPATH_WATCHED_INTRO) == PROJECT_VERSION_STRING;
}
static void set_watched_intro() {
	writefile(HARDPATH_WATCHED_INTRO, PROJECT_VERSION_STRING);
}



class ML_MainMenu : public MainLoop
{
public:
	struct Line {
		std::string text;
		std::function<void()> on_press;
		bool highlight = false;
		Rect at = {};
	};
	std::vector<Line> lines;
	vec2i size;
	TimeSpan highlight_cou;
	
	ML_MainMenu() {}
	void init()
	{
		static auto start_game = [](std::string_view args)
		{
			create(INIT_GAME, false);
			
			ArgvParse arg(args);
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
		
		if (!is_in_game())
		{
			lines.push_back({"MAIN MENU\n", {}});
			lines.push_back({"", {}});
			if (fexist(HARDPATH_REPLAY_SAVEGAME)) lines.push_back({"Continue", {[]{ start_game("--loadlast --savegame"); }}});
			lines.push_back({"New game", {[]{ start_game("--rndseed --savegame"); }}});
			lines.push_back({"Survival", {[]{ start_game("--survival --no-ffwd --rndseed"); }}});
			lines.push_back({"", {}});
			lines.push_back({"Intro", {[]{ create(INIT_INTRO); set_watched_intro(); }}, !has_watched_intro()});
			lines.push_back({"Tutorial", {[]{ start_game("--tutorial"); }}});
			lines.push_back({"", {}});
			lines.push_back({"Keybinds", {[]{ create(INIT_KEYBIND); }}});
			lines.push_back({"Options", {[]{ create(INIT_OPTIONS); }}});
			lines.push_back({"", {}});
			lines.push_back({"Exit", [this]{ delete this; }});
		}
		else
		{
			lines.push_back({"PAUSE MENU\n", {}});
			lines.push_back({"Game is automatically saved on exit.", {}});
			lines.push_back({"", {}});
			lines.push_back({"Keybinds", {[]{ create(INIT_KEYBIND); }}});
			lines.push_back({"Options", {[]{ create(INIT_OPTIONS); }}});
			lines.push_back({"Controls", {[]{ create(INIT_HELP); }}});
			lines.push_back({"", {}});
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
	void on_current()
	{
		lines.clear();
		init();
	}
	void on_event(const SDL_Event& ev)
	{
		if (ml_prev && ev.type == SDL_KEYDOWN && 
		    ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
				delete this;
	}
	void render(TimeSpan, TimeSpan passed)
	{
		bool hlight = highlight_cou.get_period_t(2) < 0.5;
		highlight_cou += passed;
		
		vec2i off = (RenderControl::get_size() - size) /2;
		for (auto& l : lines)
		{
			if (!l.on_press) {
				vig_label(l.text, off + l.at.lower(), l.at.size());
				continue;
			}
			if (vig_button(l.text, 0, l.highlight && hlight, false, off + l.at.lower(), l.at.size())) {
				l.on_press();
				return;
			}
		}
		
		// I'm bored
		vec2i sz = RenImm::get().text_size("()_.._()");
		vec2i pos = RenderControl::get_size() - sz;
		if (Rect::off_size(pos, sz).contains(vig_mouse_pos())) RenImm::get().draw_text(pos, "()_^^_()");
		else if (MainLoop::is_debug_mode) RenImm::get().draw_text(pos, "()_**_()");
		else RenImm::get().draw_text(pos, "()_.._()");
		
#ifdef PROJECT_VERSION_STRING
		pos = {0, RenderControl::get_size().y - RenImm::get().text_size(PROJECT_VERSION_STRING).y};
		RenImm::get().draw_text(pos, PROJECT_VERSION_STRING);
#endif
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
		RenImm::get().draw_rect(Rectfp::bounds({}, RenderControl::get_size()), 0xff);
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
=== CONTROLS LIST ==========		=== REPLAY PLAYBACK ========

F1       toggle this screen			1	increase speed x2
ESC      open keybinds menu			2	decrease speed x2
Pause	 pause game					3	reset speed to normal


=== GAME ===================		=== MAP ====================		=== TELEPORT ===============

See Keybinds menu					TAB		highlight visited			LMB	 select teleport
									Mouse	scroll						ESC	 exit menu


=== DEBUG ==========================================================================================

B	toggle far camera			Shift (held) debug mode					H	toggle pause
F4	toggle godmode				F7    toggle debug mode					J	step world once
0	toggle physics debug draw	P     (debug mode) toggle puppet mode	K	toggle free camera
								LMB   (debug mode) select object		L	reset free camera
								RMB   (debug mode) teleport at			Arrows  control free camera
								RMB   (puppet mode) move to

=== APP DEBUG ======================================================================================

~ + Q   exit immediatly				F2      toggle log
~ + R   reload shaders				F3      toggle fps counter
~ + F   switch fullscreen mode		~ + 1   debug menu: disable
~ + S   reset window to minimal		~ + 2   debug menu: rendering
~ + C   reload config				~ + 3   debug menu: game
~     while held, blocks non-UI input)";
		tri.build();
	}
	void on_event(const SDL_Event& ev) {
		if (ev.type == SDL_KEYDOWN && (
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



class ML_Options : public MainLoop
{
public:
	AppSettings& sets;
	LineCfg cfg;
	bool changed = false;
	vec2i zone_offset = {};
	
	void save()
	{
		if (cfg.write(sets.path_settings.c_str())) {
			for (auto& p : cfg.get_opts()) p.changed = false;
			changed = false;
			vig_message("New settings successfully saved", TimeSpan::seconds(5).ms());
		}
		else vig_infobox("Failed to save new settings!");
	}
	void discard()
	{
		if (sets.load()) {
			for (auto& p : cfg.get_opts()) p.changed = false;
			changed = false;
		}
		else {
			vig_infobox("Failed to restore old settings!\nReset to defaults.");
			defaults();
		}
	}
	void defaults()
	{
		AppSettings copy = sets;
		auto ccfg = copy.gen_cfg();
		
		sets.init_default();
		
		changed = false;
		auto& opts = cfg.get_opts();
		for (size_t i=0; i<opts.size(); ++i)
		{
			auto& a1 = ccfg.get_opts()[i].get_args();
			auto& a2 = opts[i].get_args();
			opts[i].changed = false;
			for (size_t i=0; i<a1.size(); ++i) {
				if (![&]{
				    if (false) {}
#define CMP(T, X) else if (std::holds_alternative<T>(a1[i])) return std::get<T>(a1[i]).X == std::get<T>(a2[i]).X
				    CMP(LineCfgArg_Int, v);
				    CMP(LineCfgArg_Float, v);
				    CMP(LineCfgArg_Bool, v);
				    CMP(LineCfgArg_Str, v);
				    CMP(LineCfgArg_Enum, get_int());
#undef CMP
				    return false;
				}())
				{
					opts[i].changed = true;
					changed = true;
					break;
				}
			}
		}
		
		if (changed) {
			if (bool(SoundEngine::get()) != sets.use_audio) {
				if (sets.use_audio) {
					if (!SoundEngine::init())
						vig_infobox("Failed to enable sound.\n\nSee log for details.", true);
				}
				else delete SoundEngine::get();
			}
			if (auto snd = SoundEngine::get()) {
				snd->set_master_vol(sets.audio_volume);
				snd->set_sfx_vol(sets.sfx_volume);
				snd->set_music_vol(sets.music_volume);
			}
			set_window_fs(sets.fscreen);
		}
	}
	LineCfgOption& opt(std::string_view name)
	{
		for (auto& p : cfg.get_opts()) {
			if (p.get_name() == name) {
				p.changed = true;
				return p;
			}
		}
		THROW_FMTSTR("No such option - {}", name);
	}
	void set_window_fs(int ix)
	{
		auto wnd = RenderControl::get().get_wnd();
		switch (static_cast<AppSettings::FS_Type>(ix))
		{
		case AppSettings::FS_Windowed:
		default:
			RenderControl::get().set_fscreen(RenderControl::FULLSCREEN_OFF);
			SDL_SetWindowSize(wnd, AppSettings::get().wnd_size.x, AppSettings::get().wnd_size.y);
			SDL_SetWindowPosition(wnd, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
			break;
			
		case AppSettings::FS_Maximized:
			{
				SDL_DisplayMode dm;
				SDL_GetDesktopDisplayMode(0, &dm);
				vec2i b0, b1;
				SDL_GetWindowBordersSize(wnd, &b0.y, &b0.x, &b1.y, &b1.x);
				vec2i sz = vec2i{dm.w, dm.h} - (b0 + b1);
				
				RenderControl::get().set_fscreen(RenderControl::FULLSCREEN_OFF);
				SDL_SetWindowSize(wnd, sz.x, sz.y);
				SDL_SetWindowPosition(wnd, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
			}
			break;
			
		case AppSettings::FS_Borderless:
			RenderControl::get().set_fscreen(RenderControl::FULLSCREEN_DESKTOP);
			break;
			
		case AppSettings::FS_Fullscreen:
			RenderControl::get().set_fscreen(RenderControl::FULLSCREEN_ENABLED);
			break;
		}
	}
	
	ML_Options()
	    : sets(AppSettings::get_mut()), cfg(sets.gen_cfg())
	{
		cfg.write_only_present = true;
	}
	~ML_Options() {
		if (changed) discard();
	}
	void render(TimeSpan, TimeSpan)
	{
		if (vig_button("Exit", SDL_SCANCODE_ESCAPE | vig_ScancodeFlag, changed)) {
			if (changed) save();
			delete this;
			return;
		}
		if (changed) {if (vig_button("Discard changes")) discard();}
		else vig_space_tab(vig_element_size("Discard changes").x);
		if (vig_button("Restore defaults")) defaults();
		vig_space_line();
		
		vec2i size = RenderControl::get_size() - vig_element_decor()*2;
		size.y -= vig_lo_get_next().y;
		vig_lo_push_scroll(size, zone_offset);
		
		// graphics
		
		vig_label("=== Graphics settings\n");
		
		vig_label("Fullscreen mode ");
		size_t ix = sets.fscreen;
		if (vig_selector(ix, {"Windowed", "Maximized", "Borderless", "Fullscreen"})) {
			auto& arg = std::get<LineCfgArg_Enum>(opt("fscreen").get_args()[0]);
			arg.type->man_set(arg.p, ix);
			changed = true;
			set_window_fs(ix);
		}
		vig_lo_next();
		
		vig_label("Window borders can be dragged to resize it.");
		vig_lo_next();
		
		// audio
		
		vig_label("=== Audio settings\n");
		if (auto snd = SoundEngine::get())
		{
			const double db0 = -10; // dB at threshold
			const double base = std::pow(10, -db0/10);
			const double baselog = 1/std::log(base);
			const double xthr = 0.1;
			const double ythr = 1/base;
			//
			auto cvt_from_user = [&](double& v){
				// x -> y
				if (v < xthr) v = v/xthr * ythr;
				else v = std::pow(base, (v-xthr)/(1-xthr) - 1);
			};
			auto cvt_to_user = [&](double& v){
				// y -> x
				if (v < ythr) v = v*xthr / ythr;
				else v = (std::log(v) * baselog + 1)*(1-xthr) + xthr;
			};
			
			double vol = snd->get_master_vol();
			cvt_to_user(vol);
			if (vig_slider("Master volume     ", vol)) {
				cvt_from_user(vol);
				std::get<LineCfgArg_Float>(opt("audio_volume").get_args()[0]).v = vol;
				snd->set_master_vol(vol);
				changed = true;
			}
			vig_lo_next();
			
			vol = snd->get_sfx_vol();
			cvt_to_user(vol);
			if (vig_slider("Effects           ", vol)) {
				cvt_from_user(vol);
				std::get<LineCfgArg_Float>(opt("sfx_volume").get_args()[0]).v = vol;
				snd->set_sfx_vol(vol);
				changed = true;
			}
			vig_space_tab();
			if (vig_button("Play quiet sound")) snd->once(SND_VOLUME_TEST_QUIET, {});
			if (vig_button("Play LOUD sound"))  snd->once(SND_VOLUME_TEST_LOUD,  {});
			vig_lo_next();
			
			vol = snd->get_music_vol();
			cvt_to_user(vol);
			if (vig_slider("Music             ", vol)) {
				cvt_from_user(vol);
				std::get<LineCfgArg_Float>(opt("music_volume").get_args()[0]).v = vol;
				snd->set_music_vol(vol);
				changed = true;
			}
			vig_lo_next();
			
			if (is_in_game()) vig_label("Can't disable sound while in game.\n");
			else {
				if (vig_button("Disable sound")) {
					std::get<LineCfgArg_Bool>(opt("use_audio").get_args()[0]).v = false;
					delete SoundEngine::get();
					changed = true;
				}
			}
		}
		else if (sets.use_audio) vig_label("Audio not available - initialization error (see log for details).\n");
		else
		{
			if (is_in_game()) vig_label("Can't enable sound while in game.\n");
			else {
				if (vig_button("Enable sound")) {
					std::get<LineCfgArg_Bool>(opt("use_audio").get_args()[0]).v = true;
					changed = true;
					if (!SoundEngine::init())
						vig_infobox("Failed to enable sound.\n\nSee log for details.", true);
				}
				vig_lo_next();
			}
		}
		vig_space_line();
		
		//
		
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
	
	std::future<std::function<void()>> async_init;
	
	// init args
	
	bool use_rndseed = false;
	bool no_ffwd = false;
	std::optional<uint32_t> use_seed;
	
	bool is_tutorial = false;
	bool is_survival = false;
	
	bool nodrop = false;
	bool nohunt = false;
	bool nocowlvl = false;
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
	std::string replay_loadgame;
	bool savegame_rename = false;
	
	static bool isok(ReplayInit& v) {return !std::holds_alternative<std::monostate>(v);}
	
	
	
	ML_Game() = default;
	~ML_Game() {
		if (gui) {
			gui->on_leave();
			if (savegame_rename && gui->has_game_finished()) {
				std::error_code ec;
				std::filesystem::rename(HARDPATH_REPLAY_SAVEGAME, FMT_FORMAT(HARDPATH_DEMO_TEMPLATE, date_time_fn()), ec);
				if (ec) {
					VLOGE("Failed to rename savegame replay: {}", ec.message());
					vig_infobox("Failed to remove saved game");
				}
			}
		}
		if (fexist(HARDPATH_REPLAY_CONFLICT)) {
			std::error_code ec;
			if (!std::filesystem::remove(HARDPATH_REPLAY_CONFLICT, ec))
				VLOGE("Failed to remove tmp (confilct) replay: {}", ec.message());
		}
		if (async_init.valid()) {
			VLOGW("Interrupted initialization! Waiting...");
			try {async_init.get();}
			catch (std::exception& e) {
				VLOGE("Interrupted initialization failed: {}", e.what());
			}
		}
	}
	bool parse_arg(ArgvParse& arg)
	{
		if		(arg.is("--rndseed")) use_rndseed = true;
		else if (arg.is("--seed")) use_seed = arg.i32();
		else if (arg.is("--no-ffwd")) no_ffwd = true;
		else if (arg.is("--tutorial")) {
			is_tutorial = true;
			replay_write_default = false;
		}
		else if (arg.is("--survival")) {
			is_survival = true;
		}
		else if (arg.is("--nodrop")) nodrop = true;
		else if (arg.is("--nohunt")) nohunt = true;
		else if (arg.is("--nocowlvl")) nocowlvl = true;
		
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
		else if (arg.is("--loadlast")) {
			replay_loadgame = HARDPATH_REPLAY_SAVEGAME;
			if (is_debug_mode && !fexist(replay_loadgame.c_str()))
				replay_loadgame = {};
		}
		else if (arg.is("--loadgame")) {
			replay_loadgame = arg.str();
			if (is_debug_mode && !fexist(replay_loadgame.c_str()))
				replay_loadgame = {};
		}
		else if (arg.is("--savegame")) {
			replay_init_write = ReplayInit_File{HARDPATH_REPLAY_SAVEGAME};
			savegame_rename = true;
		}
		
		else return false;
		return true;
	}
	void init()
	{
		PlayerInput::get(); // init
		init_greet = GameUI::generate_greet();
		
		async_init = std::async(std::launch::async, [this]
		{
			set_this_thread_name("ML::init");
			
			auto p_gci = std::make_unique<GameControl::InitParams>();
			auto& gci = *p_gci;
			bool is_loadgame = false;
			
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
			
			if (!replay_loadgame.empty()) {
				gci.is_loadgame = is_loadgame = true;
				replay_init_read = ReplayInit_File{std::move(replay_loadgame)};
				
				gci.loadgame_error_h = []
				{
					vigWarnbox wb{"WARNING", "Saved game is corrupted.\nLoad anyway?"};
					wb.buttons.emplace_back(vigWarnboxButton{"Yes", {}}).is_default = true;
					wb.buttons.emplace_back(vigWarnboxButton{"No", {}}).is_escape = true;
					return 0 == vig_warnbox_wait(std::move(wb));
				};
			}
			
			if (std::holds_alternative<ReplayInit_File>(replay_init_read) &&
			    std::holds_alternative<ReplayInit_File>(replay_init_write) &&
			    std::get<ReplayInit_File>(replay_init_read).fn == std::get<ReplayInit_File>(replay_init_write).fn)
			{
				auto& fn = std::get<ReplayInit_File>(replay_init_read).fn;
				std::filesystem::rename(fn, HARDPATH_REPLAY_CONFLICT);
				fn = HARDPATH_REPLAY_CONFLICT;
			}
			
			if (replay_write_default
				&& !isok(replay_init_write)
				&& !isok(replay_init_read))
			{
				replay_init_write = MainLoop::startup_date
					? ReplayInit_File{FMT_FORMAT(HARDPATH_DEMO_TEMPLATE, date_time_fn())}
					: ReplayInit_File{HARDPATH_USR_PREFIX"last.ratdemo"};
			}
			
			ReplayInitData replay_dat;
			if (isok(replay_init_write) && !isok(replay_init_read))
			{
				replay_dat.rnd_init = gci.rndg.save();
				replay_dat.fastforward = gci.fastforward_time;
				replay_dat.pmg_superman = is_superman_init;
				replay_dat.pmg_dbg_ai_rect = debug_ai_rect_init;
				replay_dat.mode_survival = is_survival;
			}
			
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
			
			if (replay_dat.incompat_version) {
				vigWarnbox wb{"WARNING",
					FMT_FORMAT("Saved game was made using another version\nplatform:\n\n{}\nversus current:\n{}\n\nLoad anyway?",
				               *replay_dat.incompat_version, get_full_platform_version())};
				wb.buttons.emplace_back(vigWarnboxButton{"Yes", {}}).is_default = true;
				wb.buttons.emplace_back(vigWarnboxButton{"No", {}}).is_escape = true;
				if (0 != vig_warnbox_wait(std::move(wb))) {
					VLOGW("Saved game incompatible version - user refused to load");
					throw GameControl::ignore_exception();
				}
			}
			
			std::visit(overloaded{
				[](std::monostate&){},
				[&](ReplayInit_File& ps){
					VLOGW("DEMO RECORD - FILE \"{}\"", ps.fn.data());
					gci.replay_wr.reset( ReplayWriter::write_file( replay_dat, ps.fn.data() ));
				},
				[&](ReplayInit_Net& ps){
					VLOGW("DEMO RECORD - NETWORK");
					gci.replay_wr.reset( ReplayWriter::write_net( replay_dat, ps.addr.data(), ps.port.data(), ps.is_serv ));
				}}
			, replay_init_write);
			
			if (gci.replay_rd) {
				gci.rndg.load( replay_dat.rnd_init );
				gci.fastforward_time = replay_dat.fastforward;
				is_superman_init     = replay_dat.pmg_superman;
				debug_ai_rect_init   = replay_dat.pmg_dbg_ai_rect;
				is_survival          = replay_dat.mode_survival;
			}
			
			// init
			
			if (is_tutorial) {
				gci.mode_ctr.reset(new GameMode_Tutorial);
				gci.terrgen = [](auto&) {return tutorial_terrain();};
				gci.spawner = tutorial_spawn;
			}
			else if (is_survival) {
				gci.mode_ctr.reset(GameMode_Survival::create());
				gci.terrgen = [](auto&) {return survival_terrain();};
				gci.spawner = survival_spawn;
			}
			else {
				gci.disable_drop = nodrop;
				gci.disable_hunters = nohunt;
				
				gci.mode_ctr.reset(GameMode_Normal::create());
				if (nocowlvl) static_cast<GameMode_Normal&>(*gci.mode_ctr).fastboot = true;
				gci.terrgen = [&](auto& rnd) {return LevelTerrain::generate({&rnd, lvl_size});};
				gci.spawner = level_spawn;
				
//				gci.terrgen = [](auto&) {return drone_test_terrain();};
//				gci.spawner = drone_test_spawn;
			}
			gctr.reset( GameControl::create(std::move(p_gci)) );
			
			return std::function<void()>([this, is_loadgame]
			{
				GameUI::InitParams pars;
				pars.ctr = gctr.get();
				pars.init_greet = std::move(init_greet);
				pars.allow_cheats = MainLoop::is_debug_mode;
				pars.start_paused = is_loadgame;
				gui.reset( GameUI::create(std::move(pars)) );
			});
		});
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
		if (async_init.valid()) {
			if (async_init.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
			async_init.get()();
		}
		
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
		
		try {
			gui->render(frame_begin, passed);
		}
		catch (GameControl::ignore_exception&) {
			delete this;
			return;
		}
		
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



class ML_Intro : public MainLoop
{
public:
	static constexpr int chars_per_line = 40;
	static constexpr std::pair<float, float> char_pause = {0.06, 0.085}; // min, max seconds (random)
	static constexpr float fast_mul = 4;
	
	enum DrawEvent {
		DE_NONE,
		DE_SHIP,
		DE_FIRE,
		DE_BOLTER,
		DE_EXPLODE,
		DE_ZOOM
	};
	struct Command {
		size_t at;
		TimeSpan delay; // after
		DrawEvent evt;
		std::function<void()> f;
	};
	
	TextRenderInfo textfull, textrd;
	size_t text_at = 0;
	
	std::vector<Command> cmds;
	size_t cmd_at = 0;
	
	bool exitrq = false;
	TimeSpan pause_tmo;
	
	DrawEvent devt = DE_NONE;
	TimeSpan tm_ship_0, tm_ship_1;
	TimeSpan tm_zoom_0, tm_zoom_1;
	
	
	
	~ML_Intro()
	{
		Postproc::get().tint_reset();
		Postproc::get().tint_default(TimeSpan{});
		Postproc::get().ui_mode(true);
		
		if (SoundEngine::get())
			SoundEngine::get()->music(nullptr);
	}
	void init()
	{
		if (SoundEngine::get())
			SoundEngine::get()->music("menu_intro", -1, false);
		//
		
		vec2i charz = RenText::get().mxc_size(FontIndex::Mono).int_ceil();
		int max_width = std::min(chars_per_line, RenderControl::get_size().x / charz.x - 3);
		
		TimeSpan total_time = {};
		std::string text;
		size_t text_cw = max_width;
	
		auto cmd = [&](TimeSpan delay, DrawEvent evt = {}, std::function<void()> f = []{}) {
			auto& c = cmds.emplace_back();
			c.at = text.size();
			for (auto& s : text) if (s == '\n') --c.at;
			c.delay = delay;
			c.evt = evt;
			c.f = std::move(f);
			total_time += delay;
		};
		auto add = [&](std::string_view str) {
			auto cvt = wrap_words(str, max_width, &text_cw);
			text += cvt;
			total_time += TimeSpan::seconds((char_pause.first + char_pause.second) /2) * cvt.length();
		};
		
		add("Dwarf planet FSC 471 g.\n\n"
		    "Mining station was built here many years ago, but "
		    "soon was abandoned. Now AIron, a galaxy-spanning mining corporation, is set to "
		    "restore and use it. An attempt to remotely activate the station revealed that "
		    "the its' AI system was online for some time and managed to set up an armed defence.\n\n");
		
		cmd(TimeSpan::seconds(1));
		cmd(TimeSpan::seconds(1), DE_SHIP);
		tm_ship_0 = total_time;
		add("To deal with it a team of experienced cybermercenaries was sent, ");
		//
		cmd(TimeSpan::seconds(1));
		add("including you.\n\n");
		
		cmd(TimeSpan::seconds(1));
		cmd(TimeSpan::seconds(0.5), DE_FIRE);
		add("Station defences turned to be extremely advanced and powerful - unexpectedly so");
		//
		cmd({}, DE_BOLTER);
		add(", because such blueprints couldn't have been stored in the databanks of non-military AI.\n\n");
		
		cmd(TimeSpan::seconds(1.5));
		cmd(TimeSpan::seconds(4.5), DE_EXPLODE);
		tm_ship_1 = total_time;
		tm_zoom_0 = total_time;
		add("You were thrown off the ship by explosion; your exoskeleton suit let "
		    "you surivive the impact with the station.\n");
		cmd({}, DE_ZOOM);
		add("Everyone else is dead. You're left on your own.\n"
		    "And you have no choice but to carry out "
		    "the job - if only to have a chance to leave this place alive.\n\n\n");
		
		cmd(TimeSpan::seconds(3), {}, []{Postproc::get().tint_seq(TimeSpan::seconds(5), FColor(0,0,0));});
		tm_zoom_1 = total_time;
		add("The AI knows you are here and will relentlessly try to hunt you "
		    "down, while you are trying to reclaim control over the station.");
		//
		
		textfull.str_a = text.c_str();
		textfull.length = text.length();
		textfull.build();
		textrd.cs.reserve(textfull.cs.size());
		
		draw_init();
		Postproc::get().ui_mode(false);
	}
	void on_event(const SDL_Event& ev)
	{
		if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
			if (!exitrq) exitrq = true;
			else if (ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
				delete this;
				return;
			}
			else if (ev.key.keysym.scancode == SDL_SCANCODE_SPACE)
				exitrq = false;
		}
		else if (ev.type == SDL_MOUSEBUTTONDOWN) {
			exitrq = true;
		}
	}
	void render(TimeSpan, TimeSpan passed)
	{
		if (exitrq) passed *= fast_mul;
		draw_background(passed.seconds());
		
		// advance text
		
		bool ended = cmd_at == cmds.size() && text_at == textfull.cs.size();
		if (ended) exitrq = true;
		else {
			pause_tmo -= passed;
			while (!pause_tmo.is_positive())
			{
				if (cmd_at != cmds.size() && cmds[cmd_at].at == text_at) {
					pause_tmo += cmds[cmd_at].delay;
					devt = cmds[cmd_at].evt;
					cmds[cmd_at].f();
					++cmd_at;
				}
				else if (text_at != textfull.cs.size()) {
					pause_tmo += TimeSpan::seconds(rnd_stat().range(char_pause.first, char_pause.second));
					textrd.cs.push_back(textfull.cs[text_at]);
					++text_at;
				}
			}
		}
		
		// draw
		
		RenImm::get().draw_text(vec2fp::one(10), textrd, RenImm::White);
		
		if (exitrq) {
			float alpha = sine_lut_norm(TimeSpan::current().tmod(TimeSpan::seconds(2)));
			alpha = 0.7 + 0.3 * alpha;
			uint32_t base = ended ? 0xff60'4000 : 0xffd0'd000;
			draw_text_hud({10,-10}, "Press ESCAPE to exit", base | clamp<int>(alpha * 255, 0, 255), false, 2);
		}
	}
	
	
	
	// calculated constants
	vec2fp screen;
	float planet_r;
	vec2fp planet_off;
	float ship_time; // seconds
	float planet_scale_speed;
	vec2fp ship_speed;
	
	const float star_max_delay = 40; // seconds
	const float star_blink_len = 5;
	//
	
	std::vector<std::tuple<vec2fp, FColor, float>> stars;
	std::vector<vec2fp> planet_lines;
	std::optional<float> planet_scale;
	vec2fp ship_pos;
	bool draw_ship = false;
	
	struct Proj {
		vec2fp orig, pos, spd;
		float t = -1, td;
		bool bolter;
		bool renew = true;
	};
	std::vector<Proj> projs;
	float proj_timeouts[2] = {};
	
	std::optional<float> explosion;
	vec2fp explo_pos;
	
	void draw_init()
	{
		screen = RenderControl::get_size();
		
		auto cdcvt = fit_rect({1366, 718}, screen);
		auto& cam = RenderControl::get().get_world_camera().mut_state();
		cam.pos = screen/2;
		cam.mag = cdcvt.first;
		float kcd = 1.f / cam.mag;
		
		planet_r = screen.y * 0.25;
		planet_off = {(screen.x - planet_r) / 2, screen.y - planet_r - 20};
		planet_scale_speed = 0.4 / (tm_zoom_1 - tm_zoom_0).seconds();
		
		int segs = 2 * M_PI * planet_r / 25.f;
		planet_lines.resize(segs);
		for (int i=0; i<segs; ++i)
			planet_lines[i] = vec2fp(planet_r, 0).fastrotate(2*M_PI*i/segs) + planet_off;
		
		const float star_r = std::pow(planet_r * kcd, 2);
		stars.reserve(60);
		for (int i=0; i<60; ++i) {
			int px, py;
			for (int tries=0; tries<10; ++tries) {
				px = screen.x/2 + rnd_stat().range_n2() * screen.x/2 * kcd;
				py = screen.y/2 + rnd_stat().range_n2() * screen.y/2 * kcd;
				if (planet_off.dist_squ(vec2fp(px, py)) > star_r)
					break;
				px = -100;
			}
			FColor clr = FColor(rnd_stat().range_n(), rnd_stat().range(0, 0.3), 1).hsv_to_rgb();
			clr.a = rnd_stat().range(0.3, 1.2);
			stars.emplace_back(vec2fp(px, py), clr, star_blink_len + rnd_stat().range_n() * star_max_delay);
		}
		
		const float ship_y0 = 150;
		ship_time = (tm_ship_1 - tm_ship_0).seconds() * 2;
		ship_speed = vec2fp(-screen.x / ship_time, (planet_off.y - ship_y0) / ship_time);
		ship_pos = {screen.x, ship_y0};
	}
	void draw_background(float passed)
	{
		// draw planet and stars
		
		if (devt == DE_ZOOM) planet_scale = 1;
		if (planet_scale) {
			*planet_scale -= planet_scale_speed * passed;
			auto ps = planet_lines;
			for (auto& p : ps) p = planet_off + (p - planet_off) * *planet_scale;
			RenAAL::get().draw_chain(ps, true, 0xa0ffc0ff, 3, 30, std::min<float>(1, *planet_scale + 0.1));
		}
		else RenAAL::get().draw_chain(planet_lines, true, 0xa0ffc0ff, 3, 30);
		
		for (auto& s : stars) {
			vec2fp pos = std::get<0>(s);
			FColor clr = std::get<1>(s);
			float& time = std::get<2>(s);
			
			time -= passed;
			if (time < star_blink_len) {
				if (time < 0) time = star_blink_len + rnd_stat().range_n() * star_max_delay;
				else {
					float t = time / star_blink_len;
					t = t > 0.5 ? (t - 0.5) * 2 : 1 - t*2;
					clr.a *= 0.2 + 0.8 * t;
				}
			}
			
			RenAAL::get().draw_line(pos, pos + vec2fp(0.1, 0), clr.to_px() | 0xff, 1, 9, clr.a);
		}
		
		// draw ship
		
		if (devt == DE_SHIP) draw_ship = true;
		if (draw_ship) {
			ship_pos += ship_speed * passed;
			
			float t = (ship_pos.x - screen.x/2) / (screen.x/2);
			FColor clr = FColor(lerp(FColor::H_yellow, FColor::H_red0, t), lerp(0.9, 0.6, t), 1).hsv_to_rgb();
			float awid = lerp(50, 15, t);
			awid *= 1 + 0.05 * sine_lut_norm(TimeSpan::current().tmod(TimeSpan::seconds(0.5)));
			RenAAL::get().draw_line(ship_pos, ship_pos + vec2fp(0.5, 0), clr.to_px(), 2, awid, 2);
		}
		
		// draw projectiles
		
		const float proj_rad = 0.9;
		const float proj_dev = deg_to_rad(20);
		const float proj_off = deg_to_rad(-15);
		
		auto add_projs = [&](int n, bool bolter) {
			for (int i=0; i<n; ++i) {
				projs.emplace_back().bolter = bolter;
				projs.back().orig = vec2fp(rnd_stat().range(proj_rad, 1) * planet_r, 0)
				                    .fastrotate(proj_off + rnd_stat().range_n2() * proj_dev) + planet_off;
			}
		};
		if (devt == DE_FIRE) add_projs(40, false);
		if (devt == DE_BOLTER) add_projs(3, true);
		
		if (!projs.empty()) {for (auto& p : proj_timeouts) p -= passed;}
		for (auto& p : projs)
		{
			if (p.t < 0) {
				if (!p.renew) continue;
				if (proj_timeouts[p.bolter] > 0) continue;
				proj_timeouts[p.bolter] += p.bolter ? 0.7 : 0.2;
				
				float bullet_speed = p.bolter ? 500 : 90;
				float ttr = ship_pos.dist(p.orig) / bullet_speed; // time to reach
				vec2fp correct = ship_speed * ttr * (1 + 0.1 * rnd_stat().range_n2());
				
				p.pos = p.orig;
				p.spd = (ship_pos + correct - p.pos).norm_to(bullet_speed);
				p.t = 1;
				p.td = 1 / (p.bolter ? 1.f : 8.f);
			}
			
			if (p.bolter) {
				vec2fp b = p.pos + p.spd;
				RenAAL::get().draw_line(p.pos, b, FColor(0.5, 1, 1).to_px(), 5, 15, 4 * p.t);
			}
			else {
				vec2fp b = p.pos + p.spd * passed;
				RenAAL::get().draw_line(p.pos, b, FColor(1, 1, 0.3).to_px(), 1, 8, 1.5 * p.t);
			}
			
			p.pos += p.spd * passed;
			p.t -= p.td * passed;
		}
		
		// BOOM
		
		if (devt == DE_EXPLODE)
		{
			explo_pos = ship_pos;
			explosion = 1;
			
			for (auto& p : projs) p.renew = false;
			draw_ship = false;
			
			Postproc::get().tint_seq(TimeSpan::seconds(0.7), FColor(1,1,1), FColor(0.3,0.3,0.2,0));
			Postproc::get().tint_seq(TimeSpan::seconds(2.5), FColor(1,1,1), FColor(0,0,0,0));
			
			for (int i=0; i<9; ++i) {
				auto& p = projs.emplace_back();
				p.bolter = false;
				p.renew = false;
				p.pos = explo_pos;
				p.spd = vec2fp(35 + 15 * rnd_stat().range_n2(), 0)
				        .fastrotate((planet_off - explo_pos).angle() + rnd_stat().range_n2() * deg_to_rad(30));
				p.t = 1;
				p.td = 1 / rnd_stat().range(3, 7);
			}
		}
		if (explosion)
		{
			FColor clr = FColor(1, 1, 0.6, 5) * *explosion;
			float wid = lerp(8, 20,  *explosion);
			float aaw = lerp(8, 250, *explosion);
			RenAAL::get().draw_line(explo_pos, explo_pos + vec2fp(0.5, 0), clr.to_px() | 0xff, wid, aaw, clr.a);
			
			*explosion -= passed / 5.;
			if (*explosion < 0) explosion.reset();
		}
		
		// reset
		
		devt = DE_NONE;
	}
};



MainLoop* MainLoop::current;

bool MainLoop::is_in_game() const
{
	for (auto p = ml_prev; p; p = p->ml_prev) {
		if (typeid(*p) == typeid(ML_Game))
			return true;
	}
	return false;
}
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
			
		case INIT_OPTIONS:
			current = new ML_Options;
			break;
			
		case INIT_INTRO:
			current = new ML_Intro;
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
