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
	
	if (char* s = SDL_GetBasePath())
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
	
	c = &cs.emplace_back( true, true, "wnd_size", [&](){ wnd_size = {i, i2}; return i > 0 && i2 > 0; });
	c->val(i);
	c->val(i2);
	
	c = &cs.emplace_back( true, true, "wnd_size_max", [&](){ wnd_size_max = i; return i == 0 || i == 1; });
	c->val(i);
	
	c = &cs.emplace_back( true, true, "fscreen", [&](){ fscreen = i; return i > -2 && i < 2; });
	c->val(i);
	
	c = &cs.emplace_back( true, true, "target_fps", [&](){ target_fps = i; return i > 0 && i <= 1000; });
	c->val(i);
	
	c = &cs.emplace_back( true, true, "set_vsync", [&](){ set_vsync = i; return i > -2 && i < 2; });
	c->val(i);
	
#define FONT(NM) \
	c = &cs.emplace_back( true, true, "font_" #NM "fn", [&](){ font_##NM##path = s; font_##NM##path.insert( 0, "res/" ); return true; }); \
	c->val(s); \
	c = &cs.emplace_back( true, true, "font_" #NM "pt", [&](){ font_##NM##pt = i; return true; }); \
	c->val(i) \
	
	FONT();
	FONT(dbg_);
	
	c = &cs.emplace_back( true, true, "font_supersample", [&](){ font_supersample = i; return i > 1; });
	c->val(i);
	
	c = &cs.emplace_back( true, true, "hole_min_alpha", [&](){ hole_min_alpha = f; return true; });
	c->val(f);
	
	c = &cs.emplace_back( true, true, "cam_mag_mul", [&](){ cam_mag_mul = f; return true; });
	c->val(f);
	
	return bc_parsefile( path_settings.c_str(), std::move(cs), 2, BC_Block::F_IGNORE_UNKNOWN );
}
