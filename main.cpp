#include <SDL2/SDL.h>
#include "core/res_image.hpp"
#include "core/tui_layer.hpp"
#include "render/control.hpp"
#include "render/gl_utils.hpp" // debug stats
#include "render/ren_aal.hpp" // test
#include "render/ren_imm.hpp"
#include "render/ren_text.hpp"
#include "render/texture.hpp" // debug stats
#include "vaslib/vas_file.hpp"
#include "vaslib/vas_log.hpp"
#include "vaslib/vas_time.hpp"
#include "console.hpp"
#include "settings.hpp"

#ifndef GAME_VERSION
#define GAME_VERSION "unspecified (alpha)"
#endif

// #define USE_RELEASE_PATHS 1



void tui_char_add_alts(); // core/tui_surface.cpp

#ifdef __unix__
#include <csignal>
#endif

static void set_signals()
{
#ifdef __unix__
	auto logsig = [](int sig){ VLOGD("UNIX signal {}", sig); };
	
	// allow app to be detached from terminal
	signal(SIGHUP, logsig);
#endif
	
	log_setup_signals();
}
static void set_wnd_pars()
{
	auto wnd = RenderControl::get().get_wnd();
	SDL_SetWindowTitle(wnd, "RAT");
	
	ImageInfo img;
	if (img.load("res/icon.png", ImageInfo::FMT_RGBA))
	{
		auto icon = img.proxy();
		SDL_SetWindowIcon( wnd, icon );
		SDL_FreeSurface( icon );
	}
	
	SDL_ShowWindow(wnd);
}



enum GamePath
{
	GAME_PATH_LOG, ///< May be empty
	GAME_PATH_RESOURCES,
	GAME_PATH_SETTINGS
};
static std::string get_game_path(GamePath path)
{
#if !USE_RELEASE_PATHS
	switch (path)
	{
	case GAME_PATH_LOG:
#ifdef __unix__
		return {};
#else
		return "rod.log";
#endif
	case GAME_PATH_RESOURCES:
		return "";
	case GAME_PATH_SETTINGS:
		return "res/settings.cfg";
	}
#else
	static bool first = true;
	static std::string base;
	static std::string pref;
	if (first)
	{
		first = false;
		
		auto s = SDL_GetBasePath();
		if (!s) VLOGE("SDL_GetBasePath failed - {}", SDL_GetError());
		else {pref = s; SDL_free(s);}
		
		s = SDL_GetPrefPath("madkrabs", "rodent");
		if (!s) VLOGE("SDL_GetPrefPath failed - {}", SDL_GetError());
		else {pref = s; SDL_free(s);}
	}
	switch (path)
	{
	case GAME_PATH_LOG:
		return pref + "game.log";
		
	case GAME_PATH_RESOURCES:
		return base;
		
	case GAME_PATH_SETTINGS:
		return pref + "settings.cfg";
	}
#endif
	VLOGX("get_game_path() invalid enum: {}", (int)path);
	return {};
}



int main( int argc, char *argv[] )
{
	TimeSpan time_init = TimeSpan::since_start();
	
//	auto ml_which = MainLoop::INIT_DEFAULT;
	int cli_logclr = -1;
	int cli_verb = -1;
	
	std::string log_filename = get_game_path(GAME_PATH_LOG);
	AppSettings::cfg_path = get_game_path(GAME_PATH_SETTINGS);
	
	for (int i = 1; i < argc; ++i)
	{
		if		(!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
		{
			printf("Usage: rodent [OPTIONS]\n");
			printf("Options:\n");
			printf("\t--log    <FILE> log will be written to this file instead of default\n");
			printf("\t--logclr <0/1>  enables or disables colors in log\n");
			printf("\t--verb   <N>    sets logging verbosity level (1 verbose, 2 debug, 3 info)\n");
			printf("\t--cfg    <FILE> sets path to settings config\n");
			return 0;
		}
		else if (!strcmp(argv[i], "--log"))
		{
			if (i == argc-1) {
				printf("Invalid option value: %s\n", argv[i]);
				return 1;
			}
			log_filename = argv[++i];
		}
		else if (!strcmp(argv[i], "--logclr"))
		{
			bool ok = (i != argc-1) && (argv[i+1][1] == 0);
			if (ok)
			{
				++i;
				if		(argv[i][0] == '0') cli_logclr = 0;
				else if (argv[i][0] == '1') cli_logclr = 1;
				else ok = false;
			}
			if (!ok) {
				printf("Invalid option value: %s\n", argv[i]);
				return 1;
			}
		}
		else if (!strcmp(argv[i], "--verb"))
		{
			if (i == argc-1) {
				printf("Invalid option value: %s\n", argv[i]);
				return 1;
			}
			
			cli_verb = atoi( argv[++i] );
			if (cli_verb < 1 || cli_verb > 3)
			{
				printf("Invalid option value: %s\n", argv[i]);
				return 1;
			}
		}
		else if (!strcmp(argv[i], "--cfg"))
		{
			if (i == argc-1) {
				printf("Invalid option value: %s\n", argv[i]);
				return 1;
			}
			AppSettings::cfg_path = argv[++i];
		}
		else {
			printf("Invalid option: %s\n", argv[i]);
			return 1;
		}
	}
	
	
	
	set_signals();
	
	if (!log_filename.empty())
    {
		LoggerSettings lsets;
		lsets.file.reset( File::open( log_filename.c_str(), File::OpenCreate ) );
		if (cli_logclr != -1) lsets.use_clr = (cli_logclr != 0);
		if (cli_verb != -1) lsets.level = static_cast< LogLevel >( cli_verb );
		lsets.apply();
    }
	VLOGI("Launched at {}", date_time_str());
	VLOGI("Game version: " GAME_VERSION);
	
	VLOGD("CLI ARGUMENTS:");
	for (int i = 0; i < argc; ++i) VLOGD( "  {}", argv[i] );
	
	{	SDL_version sv_comp;
		SDL_version sv_link;
		
		SDL_VERSION(&sv_comp);
		SDL_GetVersion(&sv_link);
		
		VLOGI("SDL version - compiled {}.{}.{}, linked {}.{}.{}",
		      sv_comp.major, sv_comp.minor, sv_comp.patch,
		      sv_link.major, sv_link.minor, sv_link.patch);
	}
	
	if (!set_current_dir( get_game_path(GAME_PATH_RESOURCES).c_str() )) VLOGW("Can't set resources directory");
	if (!AppSettings::get_mut().load())
	{
		VLOGE("Can't load settings. Check working directory - it must contain 'res' folder");
		VLOGW("Using default settings");
//		return 1;
	}
	
	if (!RenderControl::init()) return 1;
	RenderControl::get().set_vsync( true );
	
	tui_char_add_alts();
	set_wnd_pars();
	
	VLOGI("Basic initialization finished in {:6.3} seconds", (TimeSpan::since_start() - time_init).seconds());
	
//	MainLoop::init( ml_which );
//	if (!MainLoop::current)
//	{
//		VLOGE("MainLoop::init() failed");
//		return 1;
//	}
	VLOGI("Full initialization finished in {:6.3} seconds", (TimeSpan::since_start() - time_init).seconds());
	
//	{	auto s = MainLoop::current->get_wnd_postfix();
//		if (!s.empty()) {
//			s.insert(0, "RAT (");
//			s.push_back(')');
//			SDL_SetWindowTitle( RenderControl::get()->get_wnd(), s.c_str() );
//		}
//	}
	
	
	
	const int target_fps = AppSettings::get().target_fps;
	VLOGI("Target FPS: {}", target_fps);
	
	TimeSpan loop_length = TimeSpan::seconds( 1.f / target_fps );
	bool loop_limit = !RenderControl::get().has_vsync() || target_fps != 60;
	VLOGD("Main loop limiter: {}", loop_limit);
	
	TimeSpan step_counter;
	TimeSpan passed = loop_length; // render time
	TimeSpan last_time = loop_length; // processing time (for info)
	
	class L : public TUI_Layer
	{
	public:
		struct Head {
			int x, y, clr;
			TimeSpan cou, per;
		};
		std::array<Head, 20> hs;
		TimeSpan prev;
		
		L() {
			for (auto& h : hs) init(h);
			
			auto t = TimeSpan::seconds(80);
			size_t n = 100;
			for (size_t i=0; i<n; ++i) proc(t*(1.f/n));
			
			prev = TimeSpan::since_start();
		}
		void on_event(const SDL_Event& ev) {
			if (ev.type == SDL_MOUSEBUTTONDOWN)
				delete this;
		}
		void render() {
			auto next = TimeSpan::since_start();
			TimeSpan passed = next - prev;
			prev = next;
			proc(passed * 2);
		}
		void proc(TimeSpan passed)
		{
			sur.upd_any = true;
			for (auto& c : sur.cs) {
				if (c.alpha > 0.2f)
					c.alpha -= 0.2 * passed.seconds();
			}
			
			for (auto& h : hs) {
				h.cou += passed;
				while (h.cou >= h.per) {
					if (++h.y == sur.size.y) {
						init(h);
						continue;
					}
					h.cou -= h.per;
					char32_t sym = 33 + rand() % (126 - 33);
					sur.set({h.x, h.y}, {sym, h.clr});
				}
			}
		}
		void init(Head& h) {
			h.x = rand() % sur.size.x;
			h.y = -1;
			h.clr = rand() % 8;
			h.cou = {};
			h.per = TimeSpan::seconds( 0.1f * (1 + rand() % 8) );
		}
	};
//	(new L)->bring_to_top();
	
	
	
	auto& cons = Console::get();
	bool cons_shown = false;
	bool dbg_show = false;
	
	bool run = true;
	while (run)
	{
		TimeSpan loop_0 = TimeSpan::since_start();
		
//		MainLoop::current->prepare();
//		if (!MainLoop::current) break;
		
		SDL_Event ev;
		while (SDL_PollEvent(&ev))
		{
			if		(ev.type == SDL_QUIT) run = false;
			else if (ev.type == SDL_KEYDOWN)
			{
				auto &ks = ev.key.keysym;
				if (ks.mod & KMOD_CTRL)
				{
					if		(ks.scancode == SDL_SCANCODE_Q) run = false;
					else if (ks.scancode == SDL_SCANCODE_R) RenderControl::get().reload_shaders();
					else if (ks.scancode == SDL_SCANCODE_D) dbg_show = !dbg_show;
					else if (ks.scancode == SDL_SCANCODE_P) {auto& f = RenderControl::get().use_pp; f = !f;}
				}
				else if (ks.scancode == SDL_SCANCODE_GRAVE) cons_shown = !cons_shown;
			}
			
			RenderControl::get().on_event( ev );
			if (cons_shown) {
				cons.on_event( ev );
				continue;
			}
			
			if (auto t = TUI_Layer::get_stack_top())
				t->on_event(ev);
			
//			MainLoop::current->on_event(ev);
//			if (!MainLoop::current) break;
		}
		
//		while (step_counter >= MainLoop::current->step_each)
//		{
//			try {
//				step_counter -= MainLoop::current->step_each;
//				MainLoop::current->step();
//			}
//			catch (const std::exception& e)
//			{
//				VLOGC("main() unhandled exception in MainLoop::step: {}", e.what());
//				delete MainLoop::current;
//				MainLoop::current = nullptr;
//			}
//			if (!MainLoop::current) break;
//		}
		
//		MainLoop::current->render( passed );
		
		vec2i scr_size = RenderControl::get().get_size();
		RenImm::get().set_context( RenImm::DEFCTX_UI );
		
		auto dbg_str = FMT_FORMAT( "{:6.3f}\n{:6.3f}", passed.micro() / 1000.f, last_time.micro() / 1000.f );
		vec2i dbg_size = RenImm::text_size( dbg_str ) + vec2i::one(4);
		vec2i dbg_rpos = { scr_size.x - dbg_size.x, 0 };
		RenImm::get(). draw_rect( {dbg_rpos, dbg_size, true}, 0x80 );
		RenImm::get(). draw_text( dbg_rpos, dbg_str, 0x00ff00ff );
		
		if (dbg_show)
		{
			auto& imm = RenImm::get();
			auto str = FMT_FORMAT("Buffer : max {:4} KB, current {:4} KB\n"
			                      "Texture : {:4} KB\n"
								  "Postproc: {}",
			                      GLA_Buffer::dbg_size_max >>10, GLA_Buffer::dbg_size_now >>10,
			                      Texture::dbg_total_size >>10,
			                      RenderControl::get().use_pp);
			
			TextRenderInfo tri;
			tri.str_a = str.data();
			tri.length = str.length();
			tri.font = FontIndex::Debug;
			tri.build();
			imm.draw_rect({ {}, tri.size + vec2i::one(4), true }, 0x60);
			imm.draw_text({}, tri, -1);
		}
		
		if (cons_shown) cons.render();
		
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
		
		last_time = TimeSpan::since_start() - loop_0;
		if (!RenderControl::get().render( passed ))
		{
			VLOGC("Critical rendering error");
			break;
		}
		
		TimeSpan loop_total = TimeSpan::since_start() - loop_0;
		if (loop_limit && loop_total < loop_length)
		{
			passed = loop_length;
			sleep(loop_length - loop_total);
		}
		else passed = loop_total;
		step_counter += passed;
	}
	
	VLOGI("main() normal exit");
//	delete MainLoop::current;
	delete &RenderControl::get();
	SDL_Quit();
	
	return 0;
}
