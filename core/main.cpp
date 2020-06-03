#include <filesystem>
#include <SDL2/SDL.h>
#include "client/resbase.hpp"
#include "client/sounds.hpp"
#include "core/hard_paths.hpp"
#include "core/vig.hpp"
#include "render/control.hpp"
#include "render/gl_utils.hpp" // debug stats
#include "render/ren_imm.hpp"
#include "render/ren_text.hpp"
#include "render/texture.hpp" // debug stats
#include "utils/line_cfg.hpp"
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
	if (img.load(HARDPATH_APP_ICON, ImageInfo::FMT_RGBA))
	{
		auto icon = img.proxy();
		SDL_SetWindowIcon( wnd, icon );
		SDL_FreeSurface( icon );
	}
	
	SDL_ShowWindow(wnd);
}



static void platform_info()
{
#if USE_OPUSFILE
	VLOGI("USE_OPUSFILE = 1");
#else
	VLOGI("USE_OPUSFILE = 0");
#endif
#if USE_SDL_MAIN
	VLOGI("USE_SDL_MAIN = 1");
#else
	VLOGI("USE_SDL_MAIN = 0");
#endif
	
	SDL_version sv_comp;
	SDL_version sv_link;
	
	SDL_VERSION(&sv_comp);
	SDL_GetVersion(&sv_link);
	
	VLOGI("SDL version - compiled {}.{}.{}, linked {}.{}.{}",
		  sv_comp.major, sv_comp.minor, sv_comp.patch,
		  sv_link.major, sv_link.minor, sv_link.patch);
	
	VLOGI("Platform: {}", get_full_platform_version());
}



#if !USE_SDL_MAIN
#undef main
#endif

int main( int argc, char *argv[] )
{
	printf("RAT game prototype (alpha)\n");
	
	TimeSpan::since_start(); // start clock
	std::optional<bool> cli_logclr;
	LogLevel cli_verb = LogLevel::Debug;
	bool no_sound = false;
	
	bool cfg_override = false;
	MainLoop::startup_date = date_time_fn();

	ArgvParse arg;
	arg.set(argc-1, argv+1);
	try {
		while (!arg.ended())
		{
			if (arg.is("--help"))
			{
				const char *opts = R"(
Usage: rodent [OPTIONS] [MODE [MODE_OPTS]]

Options:
  --log    <FILE> override default log path
  --logclr <0/1>  enable colors in log
  -v0 -v -vv      logfile verbosity level: info (default), debug, verbose
				                   
  --cfg <FILE> override default config path
  --dump-cfg   saves default config values to "user/default.cfg" and exits
  --no-sound   overrides config and disables sound
  --snd-check  checks sound and music lists and exits

  --gldbg      create debug OpenGL context and log all GL messages as verbose
  --debugmode  enables various debug options
  --no-fndate  don't save logs and replays to files created using current time

Modes:
  --game      [default]

Mode options (--game):
  --rndseed   use random level seed
  --seed <N>  use specified level seed
  --no-ffwd   disable fast-forwarding world on init

  --tutorial  launch tutorial level
  --survival  launch survival mode

  --nodrop    disable enemy item drop
  --nohunt    disable hunters
  --nocowlvl  enable fastboot for level terminal

  --lvl-size <W> <H>  generate level of that size instead of the default

  --superman     enable enhanced godmode for player
  --dbg-ai-rect  enable smaller AI online rects (for performance)
  --save-terr    save generated terrain data

  --no-demo-record     disables default demo record (if no option specified)
  --demo-record        record replay to "user/replay_DATETIME.ratdemo"
                       default if '--no-fndate' not specified
  --last-record        record replay to "user/last.ratdemo"
                       default if '--no-fndate' is specified
  --demo-write <FILE>  record replay to specified file (adds extension)
  --demo-play  <FILE>  playback replay from file
  --demo-last          same as "--demo-play user/last.ratdemo"
  --loadgame   <FILE>  loads replay as savegame
  --loadlast           same as "--loadgame user/savegame.ratdemo"
  --savegame           record replay to savegame file + rename it after game is finished

  --demo-net       <ADDR> <PORT> <IS_SERVER>  write replay to network
  --demo-net-play  <ADDR> <PORT> <IS_SERVER>  playback replay from network
)";
				printf("%s", opts);
#ifdef _WIN32
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "CLI options (--help)", opts, nullptr);
#endif
				return 1;
			}
			else if (arg.is("--log")) {
				AppSettings::get_mut().path_log = arg.str();
			}
			else if (arg.is("--logclr")) cli_logclr = arg.flag();
			else if (arg.is("--cfg")) {
				AppSettings::get_mut().path_settings  = arg.str();
				cfg_override = true;
			}
			else if (arg.is("-v0")) cli_verb = LogLevel::Info;
			else if (arg.is("-v"))  cli_verb = LogLevel::Debug;
			else if (arg.is("-vv")) cli_verb = LogLevel::Verbose;
			else if (arg.is("--gldbg")) RenderControl::opt_gldbg = true;
			else if (arg.is("--debugmode")) MainLoop::is_debug_mode = true;
			else if (arg.is("--no-fndate")) MainLoop::startup_date = {};
			else if (arg.is("--no-sound")) no_sound = true;
			else if (arg.is("--snd-check")) {
				return SoundEngine::check_unused_sounds();
			}
			else if (arg.is("--dump-cfg")) {
				bool ok = AppSettings::get_mut().gen_cfg().write(HARDPATH_USR_PREFIX"default.cfg");
				printf("--dump-cfg: %s\n", ok? "OK" : "FAILED");
				return 1;
			}
			else if (arg.is("--game"))
			{
				if (MainLoop::current) {
					printf("Invalid argument: mode already selected\n");
					MainLoop::show_error_msg("Invalid command-line option: mode already selected");
					return 1;
				}
				MainLoop::create(MainLoop::INIT_GAME, false);
			}
			else {
				if (!MainLoop::current) MainLoop::create(MainLoop::INIT_DEFAULT_CLI, false);
				if (MainLoop::current->parse_arg(arg)) continue;
				
				printf("Invalid option: %s\n", arg.cur().c_str());
				MainLoop::show_error_msg(FMT_FORMAT("Invalid command-line option: {}", arg.cur()));
				return 1;
			}
		}
	}
	catch (std::exception& e)
	{
		printf("%s\nFailed to parse arguments\n", e.what());
		MainLoop::show_error_msg("Failed to parse command-line options");
		return 1;
	}
	
	
	
	set_signals();
	
	{	std::error_code ec;
		if (!std::filesystem::create_directories(HARDPATH_USR_DIRECTORY, ec) && ec)
			VLOGE("Can't create user directory: {}", ec.message());
	}
	
    {	std::string log_fn = AppSettings::get().path_log;
		if (log_fn.empty()) {
			if (MainLoop::startup_date)
				log_fn = fmt::format(HARDPATH_LOGFILE_FNDATE, *MainLoop::startup_date);
			else
				log_fn = HARDPATH_LOGFILE;
		}
		VLOGI("Log filename: {}", log_fn);
		
		LoggerSettings lsets = LoggerSettings::current();
		lsets.file.reset( File::open( log_fn.c_str(), File::OpenCreate | File::OpenDisableBuffer ) );
		if (!lsets.file) VLOGW("Log not written to file! (opening error)");
		
		lsets.file_level = cli_verb;
		if (cli_logclr) lsets.use_clr = *cli_logclr;
#ifdef _WIN32
		else lsets.use_clr = false;
#endif
		lsets.apply();
    }
	
	{	std::string s = "libfmt version: ";
		s += std::to_string(FMT_VERSION) + " (";
		s += std::to_string(FMT_VERSION / 10000) + '.';
		s += std::to_string(FMT_VERSION % 10000 / 100) + '.';
		s += std::to_string(FMT_VERSION % 100) + ")";
		log_write_str(LogLevel::Info, s.data());
	}
	VLOGI("Launched at {}", date_time_str());
	
	VLOGD("CLI ARGUMENTS:");
	for (int i = 0; i < argc; ++i) VLOGD( "  {}", argv[i] );
	
	platform_info();
	

	
	if (cfg_override)
	{
		if (!AppSettings::get_mut().load()) {
			VLOGE("Settings: cmd override - FAILED, ignoring");
			cfg_override = false;
		}
		else VLOGI("Settings: cmd override - loaded");
	}
	else VLOGI("Settings: cmd override - not present");
	
	if (!cfg_override)
	{
		AppSettings::get_mut().path_settings = HARDPATH_SETTINGS_USER;
		if (AppSettings::get_mut().load()) VLOGI("Settings: user override - loaded");
		else VLOGW("Settings: user override - FAILED");
	}
	
	AppSettings::get().clear_old();
	
	
	
	std::unique_ptr<vigAverage> avg_passed;
	
	auto dbg_g = vig_reg_menu(VigMenu::DebugRenderer, [&]()
	{
		vig_label_a(
			"Buffer : max {:4} KB, current {:4} KB\n"
			"Texture : {:4} KB\n",
			GLA_Buffer::dbg_size_max >> 10, GLA_Buffer::dbg_size_now >> 10,
			Texture::dbg_total_size >> 10);
		
		auto fs_val = RenderControl::get().get_fscreen();
		if (vig_button("Fullscreen on", 0, fs_val == RenderControl::FULLSCREEN_ENABLED))
		{
			if (fs_val == RenderControl::FULLSCREEN_ENABLED)
				RenderControl::get().set_fscreen(RenderControl::FULLSCREEN_OFF);
			else RenderControl::get().set_fscreen(RenderControl::FULLSCREEN_ENABLED);
		}
		if (vig_button("Fullscreen window", 0, fs_val == RenderControl::FULLSCREEN_DESKTOP))
		{
			if (fs_val == RenderControl::FULLSCREEN_DESKTOP)
				RenderControl::get().set_fscreen(RenderControl::FULLSCREEN_OFF);
			else RenderControl::get().set_fscreen(RenderControl::FULLSCREEN_DESKTOP);
		}
		vig_lo_next();
		
		if (vig_button("Reload shaders")) RenderControl::get().reload_shaders();
		vig_lo_next();
		
		avg_passed->draw();
		vig_lo_next();
	});
	
	
	
	log_write_str(LogLevel::Critical, "=== Starting renderer initialization ===");
	if (!RenderControl::init()) {
		MainLoop::show_internal_error("Renderer init failed");
		return 1;
	}
	log_write_str(LogLevel::Critical, "=== Renderer initialization finished ===");
	
	if (int set = AppSettings::get().set_vsync; set != -1)
		RenderControl::get().set_vsync(set);
	else VLOGI("set_vsync = ignored");
	
	set_wnd_pars();
	SDL_PumpEvents(); // just in case
	
	if (AppSettings::get().use_audio && !no_sound) {
		if (!SoundEngine::init())
			MainLoop::show_internal_error("Sound init failed");
	}
	else VLOGI("Sound disabled");
	
	VLOGI("Basic initialization finished in {:.3f} seconds", TimeSpan::since_start().seconds());
	
	
	
	if (!MainLoop::current) MainLoop::create(MainLoop::INIT_DEFAULT, false);
	try {MainLoop::current->init();}
	catch (std::exception& e) {
		VLOGE("MainLoop::init() failed: {}", e.what());
		MainLoop::show_internal_error("Menu init failed");
		return 1;
	}
	
	VLOGI("Full initialization finished in {:.3f} seconds", TimeSpan::since_start().seconds());
	
	
	
//	static const float loglines_mul = 11.f / RenText::get().line_height( FontIndex::Debug );
	static const float loglines_mul = 1;
	
	auto loglines_g = RenderControl::get().add_size_cb([]
	{
		vec2i sz = RenderControl::get_size() / (RenText::get().mxc_size( FontIndex::Debug ).int_ceil() * loglines_mul);
		LoggerSettings lsets = LoggerSettings::current();
		lsets.lines = sz.y;
		lsets.lines_width = sz.x;
		lsets.apply();
	});
	
	
	
	int vsync_fps = 0;
	{	SDL_DisplayMode disp_mode;
		if (SDL_GetCurrentDisplayMode(0, &disp_mode)) VLOGE("SDL_GetCurrentDisplayMode failed - {}", SDL_GetError());
		else vsync_fps = disp_mode.refresh_rate;
	}
	if (vsync_fps) VLOGI("Refresh rate specified - {}", vsync_fps);
	else {
		vsync_fps = 60;
		VLOGI("No refresh rate specified, using fallback - {}", vsync_fps);
	}
	
	int target_fps = AppSettings::get().target_fps;
	if (target_fps) VLOGI("Target FPS (manual setting): {}", target_fps);
	else {
		target_fps = vsync_fps;
		VLOGI("Target FPS (native refresh rate): {}", target_fps);
	}
	
	TimeSpan loop_length = TimeSpan::fps( target_fps );
	bool loop_limit = true;//!RenderControl::get().has_vsync() || target_fps != vsync_fps;
	VLOGD("Main loop limiter: {}", loop_limit);
	
	log_write_str(LogLevel::Critical, "=== main loop begin ===");
	
	
	
	TimeSpan passed = loop_length; // render time
	TimeSpan last_time = loop_length; // processing time (for info)
	avg_passed = std::make_unique<vigAverage>(5, 1.f/target_fps);
	
	std::vector<uint8_t> lag_spike_flags;
	lag_spike_flags.resize( TimeSpan::seconds(5) / loop_length );
	size_t lag_spike_i = 0;
	int lag_spike_count = 0;
	const TimeSpan lag_spike_tolerance = TimeSpan::ms(2);
	
	double avg_total = 0;
	size_t avg_total_n = 0;
	size_t overmax_count = 0;
	
	int passed_hack_n = 0;
	
	
	
	bool log_shown = false;
	bool debug_key_combo = false;
	bool fpscou_shown = MainLoop::is_debug_mode;
	bool run = true;
	while (run)
	{
		TimeSpan loop_0 = TimeSpan::current();
		vig_begin();
		
		SDL_Event ev;
		while (SDL_PollEvent(&ev))
		{
			if (ev.type == SDL_QUIT) {
				VLOGD("SDL_QUIT event");
				run = false;
			}
			else if (ev.type == SDL_KEYDOWN)
			{
				auto &ks = ev.key.keysym;
				if (debug_key_combo)
				{
					if		(ks.scancode == SDL_SCANCODE_Q) run = false;
					else if (ks.scancode == SDL_SCANCODE_R) RenderControl::get().reload_shaders();
					else if (ks.scancode == SDL_SCANCODE_F)
					{
						if (RenderControl::get().get_fscreen() == RenderControl::FULLSCREEN_OFF)
							RenderControl::get().set_fscreen(RenderControl::FULLSCREEN_DESKTOP);
						else RenderControl::get().set_fscreen(RenderControl::FULLSCREEN_OFF);
					}
					else if (ks.scancode == SDL_SCANCODE_S)
					{
						auto wnd = RenderControl::get().get_wnd();
						RenderControl::get().set_fscreen(RenderControl::FULLSCREEN_OFF);
						SDL_RestoreWindow(wnd);
						SDL_SetWindowSize(wnd, 600, 400);
						SDL_SetWindowPosition(wnd, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
					}
				}
				else if (ks.scancode == SDL_SCANCODE_GRAVE) debug_key_combo = true;
				else if (ks.scancode == SDL_SCANCODE_F2) log_shown = !log_shown;
				else if (ks.scancode == SDL_SCANCODE_F3) fpscou_shown = !fpscou_shown;
			}
			else if (ev.type == SDL_KEYUP)
			{
				auto &ks = ev.key.keysym;
				if (ks.scancode == SDL_SCANCODE_GRAVE) debug_key_combo = false;
			}
			
			RenderControl::get().on_event( ev );
			
			vig_on_event(ev);
			if (debug_key_combo) continue;
			
			try {MainLoop::current->on_event(ev);}
			catch (std::exception& e) {
				VLOGE("MainLoop::on_event() exception: {}", e.what());
				run = false;
				MainLoop::show_internal_error("Event handling error");
				break;
			}
			if (!MainLoop::current) {
				run = false;
				break;
			}
		}
		if (!run) break;
		
		RenImm::get().set_context(RenImm::DEFCTX_UI);
		vig_draw_start();
		vig_draw_menues();
		
		if (passed < TimeSpan::seconds(0.1)) passed_hack_n = 0; // thread lag hack
		else if (++passed_hack_n < 3) passed = loop_length;
		
		try {MainLoop::current->render( loop_0, passed );}
		catch (std::exception& e) {
			VLOGE("MainLoop::render() exception: {}", e.what());
			MainLoop::show_internal_error("Menu render failed");
			break;
		}
		if (!MainLoop::current) break;
		
		vig_draw_end();
		
		RenImm::get().set_context( RenImm::DEFCTX_UI );
		
		if (fpscou_shown)
		{
			auto dbg_str = FMT_FORMAT( "{:6.3f}\n{:6.3f}", passed.micro() / 1000.f, last_time.micro() / 1000.f );
			if (lag_spike_count) dbg_str += FMT_FORMAT("\nSkips: {:3} / 5s", lag_spike_count);
			draw_text_hud( {-1,0}, dbg_str, 0x00ff00ff );
			avg_passed->add( last_time.micro() / 1000.f, passed.seconds() );
		}
		else if (lag_spike_count)
			draw_text_hud( {-1,0}, FMT_FORMAT("Skips: {}", lag_spike_count), 0x00ff00ff );
		
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
		
		last_time = TimeSpan::current() - loop_0;
		if (!RenderControl::get().render( passed ))
		{
			VLOGC("Critical rendering error");
			MainLoop::show_internal_error("Rendering failed");
			break;
		}
		
		TimeSpan loop_total = TimeSpan::current() - loop_0;
		if (loop_limit && loop_total < loop_length)
		{
			passed = loop_length;
			precise_sleep(loop_length - loop_total);
		}
		else {
			passed = loop_total;
			if (!loop_limit) passed = loop_length;
		}
		
		if (lag_spike_flags[lag_spike_i]) --lag_spike_count;
		lag_spike_flags[lag_spike_i] = loop_total > (loop_length + lag_spike_tolerance);
		if (lag_spike_flags[lag_spike_i]) ++lag_spike_count;
		lag_spike_i = (lag_spike_i + 1) % lag_spike_flags.size();
		
		++avg_total_n;
		avg_total += (loop_total.seconds() * 1000 - avg_total) / avg_total_n;
		
		if (loop_total > loop_length + lag_spike_tolerance /*&& !MainLoop::is_debug_mode*/) {
			++overmax_count;
			VLOGD("Render lag: {:.3f} seconds on step {}", loop_total.seconds(), avg_total_n);
		}
	}
	
	SDL_HideWindow(RenderControl::get().get_wnd());
	
	VLOGI("Total run time: {:.3f} seconds", TimeSpan::since_start().seconds());
	VLOGI("Average render frame length: {} ms, {} samples", avg_total, avg_total_n);
	VLOGI("Render frame length > sleep time: {} samples", overmax_count);
	log_write_str(LogLevel::Critical, "main() normal exit");
	
	dbg_g.trigger();
	loglines_g.trigger();
	avg_passed.reset();
	
	while (MainLoop::current) delete MainLoop::current;
	delete SoundEngine::get();
	delete &ResBase::get();
	delete &RenderControl::get();
	SDL_Quit();
	
	VLOGI("main() cleanup finished");
	log_terminate_h_reset();
	return 0;
}
