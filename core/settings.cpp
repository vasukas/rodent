#include <filesystem>
#include <mutex>
#include <thread>
#include "core/hard_paths.hpp"
#include "utils/line_cfg.hpp"
#include "vaslib/vas_file.hpp"
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
	path_log = {};
	path_settings = HARDPATH_SETTINGS_USER;
	init_default();
}
bool AppSettings::load()
{
	VLOGI("AppSettings::load() from \"{}\"", path_settings);
	if (overrides.empty()) {
		return gen_cfg().read(path_settings.c_str());
	}
	
	VLOGW("AppSettings::load() using CLI overrides");
	auto s = readfile(path_settings.c_str()).value_or(std::string{});
	for (auto& v : overrides) {s += '\n'; s += v;}
	gen_cfg().read_s(s);
	return true;
}
LineCfg AppSettings::gen_cfg()
{
	std::vector<LineCfgOption> cs;
	
#define P_INT(NAME, ...)\
	cs.emplace_back(#NAME).vint(NAME, ##__VA_ARGS__)
	
#define P_FLOAT(NAME, ...)\
	cs.emplace_back(#NAME).vfloat(NAME, ##__VA_ARGS__)
	
#define P_BOOL(NAME)\
	cs.emplace_back(#NAME).vbool(NAME)
	
#define P_ENUM(NAME, TYPE, ...)\
	cs.emplace_back(#NAME).venum(NAME, LineCfgEnumType::make<TYPE>({ __VA_ARGS__ }))
	
#define P_STR(NAME)\
	cs.emplace_back(#NAME).vstr(NAME)
	
	//
	
	cs.emplace_back("wnd_size", true)
	.vint(wnd_size.x)
	.vint(wnd_size.y)
		.descr("window size");
	
	P_ENUM(fscreen, FS_Type, {FS_Windowed, "off"}, {FS_Maximized, "max"}, {FS_Borderless, "desktop"}, {FS_Fullscreen, "on"})
		.descr("fullscreen: off, max, desktop, on");
	
	P_INT(target_fps, 1000)
		.descr("frames per second, rendering. 0 is native refresh rate");
	
	P_ENUM(set_vsync, int, {-1, "dont_set"}, {0, "force_off"}, {1, "on"})
		.descr("vertical synchronization: dont_set, force_off, on");
	
	P_BOOL(use_audio).descr("enable sound");
	P_FLOAT(audio_volume).descr("master audio volume (linear)");
	P_FLOAT(sfx_volume).descr("sound effects volume (linear)");
	P_FLOAT(music_volume).descr("music volume (linear)");
	P_STR(audio_api).descr("empty string for default");
	P_STR(audio_device).descr("empty string for default");
	P_INT(audio_rate).descr("sample rate");
	P_INT(audio_samples).descr("buffer size");
	P_BOOL(use_portaudio).descr("use PortAudio backend if available - may help with latency");
	P_BOOL(use_audio_reverb).descr("disable reverb effect - may remove audio artifacts due to low performance");
	
#define FONT(NM, DESCR) \
	cs.emplace_back("font" #NM)\
	.vstr(font##NM##_path)\
	.vfloat(font##NM##_pt, 1000, 4)\
	.descr(DESCR)
	
	FONT(, "primary font - filename and size");
	FONT(_dbg, "debug info etc");
	
	P_INT(font_supersample, 8, 1)
		.descr("font atlas upscale");
	
	P_FLOAT(cam_pp_shake_str)
		.descr("camera shake effect strength");
	
	P_ENUM(interp_depth, int, {0, "0"}, {2, "2"}, {3, "3"})
		.descr("interpolation depth (0, 2 or 3)");
	
	P_INT(cursor_info_flags, 1)
		.descr("info shown on cursor - sum of: 1 weapon status, 2 shield status, 4 more status");
	        
	P_BOOL(plr_status_blink)
		.descr("enable HUD elements blinking");
	
	P_BOOL(plr_status_flare)
		.descr("enable HUD screen edge flare");
	
	return LineCfg(std::move(cs));
}
void AppSettings::init_default()
{
	wnd_size = {1024, 600};
	target_fps = 0;
	set_vsync = 1;
	fscreen = FS_Maximized;
	
	use_audio = true;
	audio_volume = 0.9;
	sfx_volume = 1;
	music_volume = 0.75;
	audio_api = {};
	audio_device = {};
	audio_rate = 48000;
	audio_samples = 1024;
	use_portaudio = true;
	use_audio_reverb = true;
	
	font_path = "data/Inconsolata-Bold.ttf";
	font_dbg_path = "data/Inconsolata.otf";
	font_pt = 16;
	font_dbg_pt = 16;
	font_supersample = 2;
	
	cam_pp_shake_str = 0.007;
	interp_depth = 3;
	
	cursor_info_flags = 1;
	plr_status_blink = true;
	plr_status_flare = true;
}
void AppSettings::clear_old() const
{
	const size_t n_save = 10; // how many saved
	
	using namespace std::filesystem;
	try {
		std::vector<std::pair<int64_t, std::string>> fs[2];
		for (auto& e : directory_iterator(HARDPATH_USR_PREFIX))
		{
			int i;
			if		(e.path().extension() == ".log")     i = 0;
			else if (e.path().extension() == ".ratdemo") i = 1;
			else continue;
			
			int64_t mod = e.last_write_time().time_since_epoch().count();
			fs[i].push_back({ mod, e.path().u8string() });
		}
		for (auto& f : fs)
		{
			std::sort(f.begin(), f.end(), [](auto& a, auto& b) {return a.first > b.first;});
			for (size_t i = n_save; i < f.size(); ++i) {
				VLOGI("Removing old file... {}", f[i].second);
				remove(f[i].second);
			}
		}
	}
	catch (filesystem_error& e) {
		VLOGE("AppSettings::clear_old() filesystem error - {}", e.what());
	}
	catch (std::exception& e) {
		VLOGE("AppSettings::clear_old() failed - {}", e.what());
	}
}
