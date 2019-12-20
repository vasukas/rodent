#ifndef SETTINGS_HPP
#define SETTINGS_HPP

#include <string>
#include "vaslib/vas_math.hpp"

struct AppSettings
{
	std::string path_log; ///< Log filename
	std::string path_resources; ///< Set as current dir; includes trailing slash
	std::string path_settings; ///< Config filename
	
	// renderer options
	vec2i wnd_size = {1024, 600};
	bool wnd_size_max = false;
	int fscreen = 0;
	
	//
	int target_fps = 30;
	int set_vsync = 1;
	
	// font options
	std::string font_path = "res/font.ttf";
	std::string font_dbg_path = {};
	float font_pt = 16;
	float font_dbg_pt = 16;
	int font_supersample = 2;
	
	// renderer effects etc
	float cam_mag_mul = 1;
	float cam_pp_shake_str = 0.01;
	
	// game
	int cursor_info_flags = 1;
	bool spawn_drop = false;
	
	
	
	static const AppSettings& get();
	static AppSettings& get_mut();
	
	bool load();
	
private:
	AppSettings();
};

#endif // SETTINGS_HPP
