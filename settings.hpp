#ifndef SETTINGS_HPP
#define SETTINGS_HPP

#include <string>
#include "vaslib/vas_math.hpp"

struct AppSettings
{
	vec2i wnd_size = {1024, 600};
	int fscreen = 0;
	
	int target_fps = 30;
	int set_vsync = 1;
	
	std::string font_path = "res/font.ttf";
	std::string font_dbg_path = {};
	
	float font_pt = 16;
	float font_dbg_pt = 16;
	
	
	
	static std::string cfg_path;
	static const AppSettings& get();
	static AppSettings& get_mut();
	
	bool load();
};

#endif // SETTINGS_HPP
