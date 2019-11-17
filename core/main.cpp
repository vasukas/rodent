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
	
	std::string log_filename = AppSettings::get().path_log; // init settings

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
  --res    <DIR>  override default resources dir

  -v0 -v -vv  different log verbosity levels, from default (info) to verbose
  --gldbg     create debug OpenGL context and log all GL messages as verbose

Modes:
  --game      [default]

Mode options (--game):
  --gpad-on   use gamepad by default (if available)
  --gpad-off  don't use gamepad by default [default]
  --cheats    allows cheats
)");
			}
			else if (arg.is("--log")) log_filename = arg.str();
			else if (arg.is("--logclr")) cli_logclr = arg.flag();
			else if (arg.is("--cfg")) AppSettings::get_mut().path_settings  = arg.str();
			else if (arg.is("--res")) AppSettings::get_mut().path_resources = arg.str();
			else if (arg.is("-v0")) cli_verb = LogLevel::Info;
			else if (arg.is("-v"))  cli_verb = LogLevel::Debug;
			else if (arg.is("-vv")) cli_verb = LogLevel::Verbose;
			else if (arg.is("--gldbg")) RenderControl::opt_gldbg = true;
			else if (arg.is("--game"))
			{
				if (MainLoop::current) {
					printf("Invalid argument: mode already selected\n");
					return 1;
				}
				MainLoop::create(MainLoop::INIT_GAME);
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
		LoggerSettings lsets = LoggerSettings::current();
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
	
	if (!set_current_dir( AppSettings::get().path_resources.c_str() )) VLOGW("Can't set resources directory");
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
		
		auto fs_val = RenderControl::get().get_fscreen();
		if (vig_button("[f] FS on", 'f', fs_val == RenderControl::FULLSCREEN_ENABLED))
		{
			if (fs_val == RenderControl::FULLSCREEN_ENABLED)
				RenderControl::get().set_fscreen(RenderControl::FULLSCREEN_OFF);
			else RenderControl::get().set_fscreen(RenderControl::FULLSCREEN_ENABLED);
		}
		if (vig_button("[w] FS wnd", 'w', fs_val == RenderControl::FULLSCREEN_DESKTOP))
		{
			if (fs_val == RenderControl::FULLSCREEN_DESKTOP)
				RenderControl::get().set_fscreen(RenderControl::FULLSCREEN_OFF);
			else RenderControl::get().set_fscreen(RenderControl::FULLSCREEN_DESKTOP);
		}
		vig_lo_next();
		
		if (vig_button("[r] Reload shaders", 'r')) RenderControl::get().reload_shaders();
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
	
	auto loglines_g = RenderControl::get().add_size_cb([]
	{
		vec2i sz = RenderControl::get_size() / (RenText::get().mxc_size( FontIndex::Debug ).int_ceil() * loglines_mul);
		LoggerSettings lsets = LoggerSettings::current();
		lsets.lines = sz.y;
		lsets.lines_width = sz.x;
		lsets.apply();
	}
	, true);
	
	
	
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
	
	
	
	bool log_shown = false;
	bool debug_key_combo = false;
	
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
				if		(ks.scancode == SDL_SCANCODE_Q && debug_key_combo) run = false;
				else if (ks.scancode == SDL_SCANCODE_R && debug_key_combo) RenderControl::get().reload_shaders();
				else if (ks.scancode == SDL_SCANCODE_F && debug_key_combo)
				{
					if (RenderControl::get().get_fscreen() == RenderControl::FULLSCREEN_OFF)
						RenderControl::get().set_fscreen(RenderControl::FULLSCREEN_DESKTOP);
					else RenderControl::get().set_fscreen(RenderControl::FULLSCREEN_OFF);
				}
				else if (ks.scancode == SDL_SCANCODE_S && debug_key_combo)
				{
					auto wnd = RenderControl::get().get_wnd();
					RenderControl::get().set_fscreen(RenderControl::FULLSCREEN_OFF);
					SDL_RestoreWindow(wnd);
					SDL_SetWindowSize(wnd, 320, 240);
					SDL_SetWindowPosition(wnd, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
				}
				else if (ks.scancode == SDL_SCANCODE_GRAVE) debug_key_combo = true;
				else if (ks.scancode == SDL_SCANCODE_F2) log_shown = !log_shown;
			}
			else if (ev.type == SDL_KEYUP)
			{
				auto &ks = ev.key.keysym;
				if (ks.scancode == SDL_SCANCODE_GRAVE) debug_key_combo = false;
			}
			
			Gamepad::on_event(ev);
			RenderControl::get().on_event( ev );
			
			vig_on_event(&ev);
			if (vig_current_menu() != VigMenu::Default || debug_key_combo) continue;
			
			try {MainLoop::current->on_event(ev);}
			catch (std::exception& e) {
				VLOGE("MainLoop::on_event() exception: {}", e.what());
				run = false;
				break;
			}
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
		
		try {MainLoop::current->render( passed );}
		catch (std::exception& e) {
			VLOGE("MainLoop::render() exception: {}", e.what());
			break;
		}
		if (!MainLoop::current) break;
		
		vig_draw_end();
		
		RenImm::get().set_context( RenImm::DEFCTX_UI );
		
		auto dbg_str = FMT_FORMAT( "{:6.3f}\n{:6.3f}", passed.micro() / 1000.f, last_time.micro() / 1000.f );
		draw_text_hud( {-1,0}, dbg_str, 0x00ff00ff );
		avg_passed->add( last_time.micro() / 1000.f, passed.seconds() );
		
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
	delete &RenderControl::get();
	SDL_Quit();
	
	VLOGI("main() cleanup finished");
	log_terminate_h_reset();
	return 0;
}
