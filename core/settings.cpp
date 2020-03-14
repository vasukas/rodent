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
#if !USE_RELEASE_PATHS
	path_log = HARDPATH_LOGFILE;
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
	
#define P_ENUM(NAME, ...)\
	cs.emplace_back(#NAME).venum(NAME, LineCfgEnumType::make<int>({ __VA_ARGS__ }))
	
	//
	
	cs.emplace_back("wnd_size", true, [&]{return wnd_size.x > 0 && wnd_size.y > 0;})
	.vint(wnd_size.x)
	.vint(wnd_size.y);
	
	P_BOOL(wnd_size_max);
	P_ENUM(fscreen, {0, "off"}, {1, "on"}, {-1, "desktop"});
	
	P_INT(target_fps, i > 0 && i <= 1000);
	P_ENUM(set_vsync, {-1, "dont_set"}, {0, "force_off"}, {1, "on"});
	
#define FONT(NM) \
	cs.emplace_back("font" #NM, true, [&]{font##NM##_path.insert(0, HARDPATH_DATA_PREFIX); return font##NM##_pt > 0;})\
	.vstr(font##NM##_path)\
	.vfloat(font##NM##_pt)
	
	FONT();
	FONT(_dbg);
	P_INT(font_supersample, i >= 1);
	
	P_FLOAT(cam_pp_shake_str, i >= 0);
	P_INT(interp_depth, i == 0 || i == 2 || i == 3);
	
	P_INT(cursor_info_flags, i >= 0 && i <= 1);
	P_BOOL(plr_status_blink);
	P_BOOL(plr_status_flare);
	
	return LineCfg(std::move(cs)).read(path_settings.c_str());
}
