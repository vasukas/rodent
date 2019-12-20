#include <SDL2/SDL_filesystem.h>
#include "vaslib/vas_log.hpp"
#include "utils/block_cfg.hpp"
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
	path_log = "rodent.log";
	path_resources = "";
	path_settings = "res/settings.cfg";
	
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
	VLOGI( "AppSettings::load() from \"{}\"", path_settings );
	std::vector <BC_Cmd> cs;
	
	int i, i2;
	std::string s;
	BC_Cmd* c;
	float f;
	
#define P_INT(NAME, CHECK)\
	c = &cs.emplace_back( true, true, #NAME, [&](){ NAME = i; return CHECK; }); \
	c->val(i)
	
#define P_FLOAT(NAME, CHECK)\
	c = &cs.emplace_back( true, true, #NAME, [&](){ NAME = f; return CHECK; }); \
	c->val(f)
	
#define P_BOOL(NAME)\
	c = &cs.emplace_back( true, true, #NAME, [&](){ NAME = i; return i == 0 || i == 1; }); \
	c->val(i)
	
#define P_TERN(NAME)\
	c = &cs.emplace_back( true, true, #NAME, [&](){ NAME = i; return i > -2 && i < 2; }); \
	c->val(i)
	
	//
	
	c = &cs.emplace_back( true, true, "wnd_size", [&](){ wnd_size = {i, i2}; return i > 0 && i2 > 0; });
	c->val(i);
	c->val(i2);
	
	P_BOOL(wnd_size_max);
	P_TERN(fscreen);
	
	P_INT(target_fps, i > 0 && i <= 1000);
	P_TERN(set_vsync);
	
#define FONT(NM) \
	c = &cs.emplace_back( true, true, "font_" #NM "fn", [&](){ font_##NM##path = s; font_##NM##path.insert( 0, "res/" ); return true; }); \
	c->val(s); \
	c = &cs.emplace_back( true, true, "font_" #NM "pt", [&](){ font_##NM##pt = i; return true; }); \
	c->val(i) \
	
	FONT();
	FONT(dbg_);
	P_INT(font_supersample, i >= 1);
	
	P_FLOAT(cam_mag_mul, true);
	P_FLOAT(cam_pp_shake_str, true);
	
	P_INT(cursor_info_flags, i >= 0 && i <= 1);
	P_BOOL(spawn_drop);
	
	return bc_parsefile( path_settings.c_str(), std::move(cs), 2, BC_Block::F_IGNORE_UNKNOWN );
}
