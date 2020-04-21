#ifndef SETTINGS_HPP
#define SETTINGS_HPP

#include <string>
#include "vaslib/vas_math.hpp"

struct AppSettings
{
	std::string path_log; ///< Log filename
	std::string path_log_date; ///< Log filename template for date
	std::string path_resources; ///< Set as current dir; includes trailing slash
	std::string path_settings; ///< Config filename
	
	int userdata_size_limit; ///< kilobytes
	
	// renderer options
	vec2i wnd_size;
	bool wnd_size_max;
	int fscreen;
	int target_fps;
	int set_vsync;
	
	// font options
	std::string font_path;
	std::string font_dbg_path;
	float font_pt;
	float font_dbg_pt;
	int font_supersample;
	
	// renderer effects etc
	float cam_pp_shake_str;
	int interp_depth;
	
	enum AAL_Type {AAL_OldFuzzy, AAL_CrispGlow, AAL_Clear};
	AAL_Type aal_type;
	
	// game
	int cursor_info_flags;
	bool plr_status_blink;
	bool plr_status_flare;
	
	
	
	static const AppSettings& get();
	static AppSettings& get_mut();
	
	bool load();
	void init_default();
	
private:
	AppSettings();
};

#endif // SETTINGS_HPP
