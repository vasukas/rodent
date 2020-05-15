#ifndef SETTINGS_HPP
#define SETTINGS_HPP

#include <string>
#include "vaslib/vas_math.hpp"

struct LineCfg;

struct AppSettings
{
	std::string path_log; ///< If not set (empty), template name is used
	std::string path_settings; ///< Config filename
	
	// renderer options
	vec2i wnd_size;
	bool wnd_size_max;
	int fscreen;
	int target_fps;
	int set_vsync;
	
	// sound options
	bool use_audio;
	float audio_volume;
	float music_volume;
	std::string audio_api;
	std::string audio_device;
	int audio_rate;
	int audio_samples;
	
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
	
	LineCfg gen_cfg();
	bool load(); ///< Re-loads from file, triggers callbacks
	void init_default(); ///< Triggers callbacks
	void trigger_cbs();
	
	/// Adds callback which called on option change. Returns callback deleter
	[[nodiscard]] RAII_Guard add_cb(std::function<void()> cb, bool call_now = true);
	
	void clear_old() const; ///< Deletes old log and replay files
	
private:
	std::vector<std::function<void()>> cbs;
	AppSettings();
};

#endif // SETTINGS_HPP
