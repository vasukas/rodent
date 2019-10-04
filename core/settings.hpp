#ifndef SETTINGS_HPP
#define SETTINGS_HPP

#include <string>
#include "vaslib/vas_math.hpp"

struct AppSettings
{
	std::string path_log; ///< Log filename
	std::string path_resources; ///< Set as current dir; includes trailing slash
	std::string path_settings; ///< Config filename
	
	vec2i wnd_size = {1024, 600};
	int fscreen = 0;
	
	int target_fps = 30;
	int set_vsync = 1;
	
	std::string font_path = "res/font.ttf";
	std::string font_dbg_path = {};
	
	float font_pt = 16;
	float font_dbg_pt = 16;

	int font_supersample = 2;
	
	bool use_particles_pp = true;
	bool use_particles_bloom = true;
	
	
	
	static const AppSettings& get();
	static AppSettings& get_mut();
	
	bool load();
	
private:
	AppSettings();
};

#endif // SETTINGS_HPP
