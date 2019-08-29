#include "vaslib/vas_log.hpp"
#include "utils/block_cfg.hpp"
#include "settings.hpp"

std::string AppSettings::cfg_path = "<UNSET>";
static AppSettings app_sets;

const AppSettings& AppSettings::get() { return app_sets; }
AppSettings& AppSettings::get_mut() { return app_sets; }

bool AppSettings::load()
{
	VLOGI( "AppSettings::load() from \"{}\"", cfg_path );
	std::vector <BC_Cmd> cs;
	
	int i, i2;
	std::string s;
	BC_Cmd* c;
	
	c = &cs.emplace_back( true, true, "wnd_size", [&](){ wnd_size = {i, i2}; return i > 0 && i2 > 0; });
	c->val(i);
	c->val(i2);
	
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
	
	return bc_parsefile( cfg_path.c_str(), std::move(cs), 2, BC_Block::F_IGNORE_UNKNOWN );
}
