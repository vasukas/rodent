#ifndef SETTINGS_HPP
#define SETTINGS_HPP

#include <string>
#include "vaslib/vas_math.hpp"

struct LineCfg;

struct AppSettings
{
	std::string path_log; ///< If not set (empty), template name is used
	std::string path_settings; ///< Config filename
	std::vector<std::string> overrides;
	
	// renderer options
	vec2i wnd_size;
	int target_fps;
	int set_vsync;
	
	enum FS_Type {FS_Windowed, FS_Maximized, FS_Borderless, FS_Fullscreen};
	FS_Type fscreen;
	
	// sound options
	bool use_audio;
	float audio_volume;
	float sfx_volume;
	float music_volume;
	std::string audio_api;
	std::string audio_device;
	int audio_rate;
	int audio_samples;
	bool use_portaudio;
	bool use_audio_reverb;
	
	// font options
	std::string font_path;
	std::string font_dbg_path;
	float font_pt;
	float font_dbg_pt;
	int font_supersample;
	
	// renderer effects etc
	float cam_pp_shake_str;
	int interp_depth;
	
	// game
	int cursor_info_flags;
	bool plr_status_blink;
	bool plr_status_flare;
	
	
	
	static const AppSettings& get();
	static AppSettings& get_mut();
	
	LineCfg gen_cfg();
	bool load(); ///< Re-loads from file, triggers callbacks
	void init_default();
	
	void clear_old() const; ///< Deletes old log and replay files
	
private:
	AppSettings();
};

#endif // SETTINGS_HPP
