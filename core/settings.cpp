#include <SDL2/SDL_filesystem.h>
#include "core/hard_paths.hpp"
#include "utils/line_cfg.hpp"
#include "vaslib/vas_log.hpp"
#include "settings.hpp"

const AppSettings& AppSettings::get() { return get_mut(); }
AppSettings& AppSettings::get_mut()
{
	static std::unique_ptr<AppSettings> sets;
	if (!sets) sets.reset(new AppSettings);
	return *sets;
}
AppSettings::AppSettings()
{
	init_default();
	
#if !USE_RELEASE_PATHS
	path_log = HARDPATH_LOGFILE;
	path_log_date = HARDPATH_LOGFILE_FNDATE;
	path_resources = "";
	path_settings = HARDPATH_SETTINGS_USER;
	
#else
#warning Not tested
	
	std::string base;
	std::string pref;
	
	if (char* s = SDL_GetBasePath())
	{
		base = s;
		SDL_free(s);
		VLOGD("SDL_GetBasePath = {}", base);
	}
	else
	{
		base = ".";
		VLOGE("SDL_GetBasePath failed - {}", SDL_GetError());
	}
	
	if (char* s = SDL_GetPrefPath("madkrabs", "rodent"))
	{
		pref = s;
		SDL_free(s);
		VLOGD("SDL_GetPrefPath = {}", pref);
	}
	else
	{
		pref = ".";
		VLOGE("SDL_GetPrefPath failed - {}", SDL_GetError());
	}
	
	path_log = pref + "game.log";
	path_resources = base;
	path_settings = pref + "settings.cfg";
#endif
}
bool AppSettings::load()
{
	VLOGI("AppSettings::load() from \"{}\"", path_settings);
	std::vector<LineCfgOption> cs;
	
#define P_INT(NAME, CHECK)\
	cs.emplace_back(#NAME, true, [&]{int i = NAME; return CHECK;}).vint(NAME)
	
#define P_FLOAT(NAME, CHECK)\
	cs.emplace_back(#NAME, true, [&]{float i = NAME; return CHECK;}).vfloat(NAME)
	
#define P_BOOL(NAME)\
	cs.emplace_back(#NAME).vbool(NAME)
	
#define P_ENUM(NAME, TYPE, ...)\
	cs.emplace_back(#NAME).venum(NAME, LineCfgEnumType::make<TYPE>({ __VA_ARGS__ }))
	
	//
	
	P_INT(userdata_size_limit, true || i)
		.descr("Size limit of userdata directory, KB (kilobytes)");
	
	cs.emplace_back("wnd_size", true, [&]{return wnd_size.x > 0 && wnd_size.y > 0;})
	.vint(wnd_size.x)
	.vint(wnd_size.y)
		.descr("window size");
	
	P_BOOL(wnd_size_max)
		.descr("start maximized (not fullscreen)");
	
	P_ENUM(fscreen, int, {0, "off"}, {1, "on"}, {-1, "desktop"})
		.descr("fullscreen: off, on, desktop");
	
	P_INT(target_fps, i > 0 && i <= 1000)
		.descr("frames per second, rendering");
	
	P_ENUM(set_vsync, int, {-1, "dont_set"}, {0, "force_off"}, {1, "on"})
		.descr("vertical synchronization: dont_set, force_off, on");
	
#define FONT(NM, DESCR) \
	cs.emplace_back("font" #NM, true, [&]{font##NM##_path.insert(0, HARDPATH_DATA_PREFIX); return font##NM##_pt > 0;})\
	.vstr(font##NM##_path)\
	.vfloat(font##NM##_pt)\
	.descr(DESCR)
	
	FONT(, "primary font - filename and size");
	FONT(_dbg, "debug info etc");
	
	P_INT(font_supersample, i >= 1)
		.descr("font atlas upscale");
	
	P_FLOAT(cam_pp_shake_str, i >= 0)
		.descr("camera shake effect strength");
	
	P_INT(interp_depth, i == 0 || i == 2 || i == 3)
		.descr("interpolation depth (0, 2 or 3)");

	P_ENUM(aal_type, AAL_Type, {AAL_OldFuzzy, "old_fuzzy"}, {AAL_CrispGlow, "crisp_glow"}, {AAL_Clear, "clear"})
		.descr("glowing lines type: old_fuzzy, crisp_glow, clear");
	
	P_INT(cursor_info_flags, i >= 0 && i <= 1)
		.descr("info shown on cursor - sum of: 1 weapon status, 2 shield status, 4 more status");
	        
	P_BOOL(plr_status_blink)
		.descr("enable HUD elements blinking");
	
	P_BOOL(plr_status_flare)
		.descr("enable HUD screen edge flare");
	
	return LineCfg(std::move(cs)).read(path_settings.c_str());
}
void AppSettings::init_default()
{
	userdata_size_limit = 1024 * 16;
	
	wnd_size = {1024, 600};
	wnd_size_max = true;
	fscreen = 0;
	target_fps = 60;
	set_vsync = 1;
	
	font_path = "data/Inconsolata-Bold.ttf";
	font_dbg_path = "data/Inconsolata.otf";
	font_pt = 16;
	font_dbg_pt = 16;
	font_supersample = 2;
	
	cam_pp_shake_str = 0.007;
	interp_depth = 3;
	aal_type = AAL_OldFuzzy;
	
	cursor_info_flags = 1;
	plr_status_blink = true;
	plr_status_flare = true;
}
