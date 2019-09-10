#include <SDL2/SDL.h>
#include "client/gamepad.hpp"
#include "core/vig.hpp"
#include "render/control.hpp"
#include "render/gl_utils.hpp" // debug stats
#include "render/ren_imm.hpp"
#include "render/ren_text.hpp"
#include "render/texture.hpp" // debug stats
#include "utils/res_image.hpp"
#include "vaslib/vas_file.hpp"
#include "vaslib/vas_log.hpp"
#include "vaslib/vas_time.hpp"
#include "main_loop.hpp"
#include "settings.hpp"



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
		return "rod.log";
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
		else {base = s; SDL_free(s);}
		
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
	VLOGX("get_game_path() invalid enum: {}", static_cast<int>(path));
	return {};
}
static void platform_info()
{
	SDL_version sv_comp;
	SDL_version sv_link;
	
	SDL_VERSION(&sv_comp);
	SDL_GetVersion(&sv_link);
	
	VLOGI("SDL version - compiled {}.{}.{}, linked {}.{}.{}",
		  sv_comp.major, sv_comp.minor, sv_comp.patch,
		  sv_link.major, sv_link.minor, sv_link.patch);
	
#ifdef __clang__
	VLOGI("Compiled with clang {}.{}.{}", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
	VLOGI("Compiled with GCC {}.{}.{}", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
	VLOGI("Compiled with MSVC {}", _MSC_VER);
#else
	VLOGI("Compiled with unknown");
#endif
	
#if defined(__linux__)
	auto ps = "Linux";
#elif defined(__unix__)
	auto ps = "UNIX-like";
#elif defined(_WIN32)
	auto ps = "Windows";
#endif
	
#if INTPTR_MAX == INT64_MAX
	int bs = 64;
#elif INTPTR_MAX == INT32_MAX
	int bs = 32;
#else
	int bs = 0;
#endif
	
	VLOGI("Platform - {} ({} bits)", ps, bs);
}



int main( int argc, char *argv[] )
{
	printf("RAT game prototype (alpha)\n");
#if !USE_RELEASE_PATHS
	printf("Using local paths\n");
#else
	printf("Using $HOME paths\n");
#endif
	
	TimeSpan time_init = TimeSpan::since_start();
	std::optional<bool>     cli_logclr;
	std::optional<LogLevel> cli_verb;
	
	std::string log_filename = get_game_path(GAME_PATH_LOG);
	AppSettings::cfg_path = get_game_path(GAME_PATH_SETTINGS);

	ArgvParse arg;
	arg.set(argc-1, argv+1);
	try {
		while (!arg.ended())
		{
			if (arg.is("--help"))
			{
				printf("%s", R"(
Usage: rodent [OPTIONS] [MODE [MODE_OPTS]]

Options:
  --log    <FILE> override default log path
  --logclr <0/1>  enable colors in log
  --cfg    <FILE> override default config path

  -v0 -v -vv  different log verbosity level, from default (info) to verbose
  --gldbg     create debug OpenGL context and log all GL messages as verbose
				       
Modes (to see options use --modehelp):
  --game      default
)");
			}
			else if (arg.is("--log")) log_filename = arg.str();
			else if (arg.is("--logclr")) cli_logclr = arg.flag();
			else if (arg.is("--cfg")) AppSettings::cfg_path = arg.str();
			else if (arg.is("-v0")) cli_verb = LogLevel::Info;
			else if (arg.is("-v"))  cli_verb = LogLevel::Debug;
			else if (arg.is("-vv")) cli_verb = LogLevel::Verbose;
			else if (arg.is("--gldbg")) RenderControl::opt_gldbg = true;
			else if (arg.is("--game")) MainLoop::create(MainLoop::INIT_GAME);
			else if (arg.is("--modehelp"))
			{
				if (!MainLoop::current) printf("No mode selected ('--modehelp')\n");
				else if (!MainLoop::current->parse_arg(arg)) printf("Mode has no options\n");
				return 1;
			}
			else if (MainLoop::current && MainLoop::current->parse_arg(arg)) continue;
			else {
				printf("Invalid option: %s\n", arg.cur().c_str());
				return 1;
			}
		}
	}
	catch (std::exception& e)
	{
		printf("%s\nFailed to parse arguments\n", e.what());
		return 1;
	}
	
	
	
	set_signals();
	
	if (!log_filename.empty())
    {
		LoggerSettings lsets;
		lsets.file.reset( File::open( log_filename.c_str(), File::OpenCreate | File::OpenDisableBuffer ) );
		if (!lsets.file) VLOGW("Log not written to file!");
		
		if (cli_verb) lsets.level = *cli_verb;
		if (cli_logclr) lsets.use_clr = *cli_logclr;
#ifdef _WIN32
		else lsets.use_clr = false;
#endif
		lsets.apply();
    }
	VLOGI("Launched at {}", date_time_str());
	
	VLOGD("CLI ARGUMENTS:");
	for (int i = 0; i < argc; ++i) VLOGD( "  {}", argv[i] );
	
	platform_info();
	
	if (!set_current_dir( get_game_path(GAME_PATH_RESOURCES).c_str() )) VLOGW("Can't set resources directory");
	if (!AppSettings::get_mut().load())
	{
		VLOGE("Can't load settings. Check working directory - it must contain 'res' folder");
		VLOGW("Using default settings");
//		return 1;
	}
	
	
	
	std::unique_ptr<vigAverage> avg_passed;
	
	auto dbg_g = vig_reg_menu(VigMenu::DebugRenderer, [&]()
	{
		vig_label_a(
			"Buffer : max {:4} KB, current {:4} KB\n"
			"Texture : {:4} KB\n",
			GLA_Buffer::dbg_size_max >> 10, GLA_Buffer::dbg_size_now >> 10,
			Texture::dbg_total_size >> 10);
		vig_checkbox(RenderControl::get().use_pp, "[p] Postproc", 'p');
		if (vig_button("[m] Reload postproc chain", 'm')) RenderControl::get().reload_pp();
		vig_lo_next();
		avg_passed->draw();
		vig_lo_next();
	});
	
	
	
	if (AppSettings::get().fscreen == 1)
		RenderControl::opt_fullscreen = true;
	
	log_write_str(LogLevel::Critical, "=== Starting renderer initialization ===");
	if (!RenderControl::init()) return 1;
	log_write_str(LogLevel::Critical, "=== Renderer initialization finished ===");
	
	if (AppSettings::get().fscreen == -1)
		RenderControl::get().set_fscreen( RenderControl::FULLSCREEN_DESKTOP );
	
	if (int set = AppSettings::get().set_vsync; set != -1)
		RenderControl::get().set_vsync(set);
	else VLOGI("set_vsync = ignored");
	
	set_wnd_pars();
	SDL_PumpEvents(); // just in case
	
	VLOGI("Basic initialization finished in {:.3f} seconds", (TimeSpan::since_start() - time_init).seconds());
	
	
	
	if (!MainLoop::current) MainLoop::create(MainLoop::INIT_DEFAULT);
	try {MainLoop::current->init();}
	catch (std::exception& e) {
		VLOGE("MainLoop::init() failed: {}", e.what());
		return 1;
	}
	
	VLOGI("Full initialization finished in {:.3f} seconds", (TimeSpan::since_start() - time_init).seconds());
	
	
	
//	static const float loglines_mul = 11.f / RenText::get().line_height( FontIndex::Debug );
	static const float loglines_mul = 1;
	
	auto loglines_upd = []
	{
		vec2i sz = RenderControl::get_size() / (RenText::get().mxc_size( FontIndex::Debug ).int_ceil() * loglines_mul);
		LoggerSettings lsets;
		lsets.lines = sz.y;
		lsets.lines_width = sz.x;
		lsets.apply();
	};
	auto loglines_g = RenderControl::get().add_size_cb(loglines_upd);
	loglines_upd();
	
	
	
	int vsync_fps = 0;
	{	SDL_DisplayMode disp_mode;
		if (SDL_GetCurrentDisplayMode(0, &disp_mode)) VLOGE("SDL_GetCurrentDisplayMode failed - {}", SDL_GetError());
		else vsync_fps = disp_mode.refresh_rate;
	}
	if (vsync_fps) VLOGI("Refresh rate specified - {}", vsync_fps);
	else {
		vsync_fps = 60;
		VLOGI("No refresh rate specified, using default - {}", vsync_fps);
	}
	
	const int target_fps = AppSettings::get().target_fps;
	VLOGI("Target FPS: {}", target_fps);
	
	TimeSpan loop_length = TimeSpan::fps( target_fps );
	bool loop_limit = true;//!RenderControl::get().has_vsync() || target_fps != vsync_fps;
	VLOGD("Main loop limiter: {}", loop_limit);
	
	log_write_str(LogLevel::Critical, "=== main loop begin ===");
	
	TimeSpan passed = loop_length; // render time
	TimeSpan last_time = loop_length; // processing time (for info)
	avg_passed = std::make_unique<vigAverage>(5, 1.f/target_fps);
	
	
	
//	bool cons_shown = false;
	bool log_shown = false;
	int input_lock = 0;
	
	bool run = true;
	while (run)
	{
		TimeSpan loop_0 = TimeSpan::since_start();
		vig_begin();
		
		SDL_Event ev;
		while (SDL_PollEvent(&ev))
		{
			if		(ev.type == SDL_QUIT) run = false;
			else if (ev.type == SDL_KEYDOWN)
			{
				auto &ks = ev.key.keysym;
				if (ks.mod & KMOD_CTRL)
				{
					if		(ks.scancode == SDL_SCANCODE_Q) {
						run = false;
						continue;
					}
					else if (ks.scancode == SDL_SCANCODE_R) {
						RenderControl::get().reload_shaders();
						continue;
					}
					else if (ks.scancode == SDL_SCANCODE_F) {
						if (RenderControl::get().get_fscreen() != RenderControl::FULLSCREEN_OFF)
							RenderControl::get().set_fscreen( RenderControl::FULLSCREEN_OFF );
						else
							RenderControl::get().set_fscreen( RenderControl::FULLSCREEN_ENABLED );
						continue;
					}
				}
//				else if (ks.scancode == SDL_SCANCODE_GRAVE) {cons_shown = !cons_shown; dbg_show = false;}
				else if (ks.scancode == SDL_SCANCODE_GRAVE) ++input_lock;
				else if (ks.scancode == SDL_SCANCODE_F2) log_shown = !log_shown;
			}
			else if (ev.type == SDL_KEYUP)
			{
				auto &ks = ev.key.keysym;
				if (ks.scancode == SDL_SCANCODE_GRAVE) --input_lock;
			}
			
			RenderControl::get().on_event( ev );
			
//			if (cons_shown) {
//				Console::get().on_event( ev );
//				continue;
//			}
			
			vig_on_event(&ev);
			if (vig_current_menu() != VigMenu::Default || input_lock) continue;
			
			MainLoop::current->on_event(ev);
			if (!MainLoop::current) {
				run = false;
				break;
			}
		}
		
		if (!run) break;
		Gamepad::update();
		
		RenImm::get().set_context(RenImm::DEFCTX_UI);
		vig_draw_start();
		vig_draw_menues();
		
		MainLoop::current->render( passed );
		if (!MainLoop::current) break;
		
		vig_draw_end();
		
		RenImm::get().set_context( RenImm::DEFCTX_UI );
		
		auto dbg_str = FMT_FORMAT( "{:6.3f}\n{:6.3f}", passed.micro() / 1000.f, last_time.micro() / 1000.f );
		RenImm::get(). draw_text_hud( {-1,0}, dbg_str, 0x00ff00ff );
		avg_passed->add( last_time.micro() / 1000.f, passed.seconds() );
		
//		if (cons_shown) Console::get().render();
		if (log_shown)
		{
			vec2fp cz = RenText::get().mxc_size(FontIndex::Debug);
			std::vector<std::pair<LogLevel, std::string>> ls;
			size_t ptr = log_get_lines(ls);
			
			for (size_t i=0; i<ls.size(); ++i)
			{
				uint32_t clr;
				switch (ls[i].first)
				{
				case LogLevel::Ignored: clr = 0; break;
				case LogLevel::Verbose: clr = 0xffffffff; break;
				case LogLevel::Debug:   clr = 0x00ffffff; break;
				case LogLevel::Info:    clr = 0x00ff00ff; break;
				case LogLevel::Warning: clr = 0xff8000ff; break;
				case LogLevel::Error:   clr = 0xff0000ff; break;
				case LogLevel::Critical:clr = 0xff00ffff; break;
				}
				
				float y = i * cz.y;
				RenImm::get().draw_rect({{0, y}, {ls[i].second.length() * cz.x, cz.y}, true}, i == ptr? 0x00800080 : 0x80);
				RenImm::get().draw_text({0, y}, ls[i].second, clr, false, loglines_mul, FontIndex::Debug);
			}
		}
		
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
	}
	
	log_write_str(LogLevel::Critical, "main() normal exit");
	
	dbg_g.trigger();
	loglines_g.trigger();
	avg_passed.reset();
	
	delete MainLoop::current;
//	delete &Console::get();
	delete &RenderControl::get();
	SDL_Quit();
	
	VLOGI("main() cleanup finished");
	log_terminate_h_reset();
	return 0;
}
