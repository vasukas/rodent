#include <filesystem>
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
	if (!fexist(path_settings.c_str())) {
		VLOGW("AppSettings::load() file doesn't exist or isn't accessible");
		return true;
	}
	bool ok = gen_cfg().read(path_settings.c_str());
	trigger_cbs();
	return ok;
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
	
	P_BOOL(wnd_size_max)
		.descr("start maximized (not fullscreen)");
	
	P_ENUM(fscreen, int, {0, "off"}, {1, "on"}, {-1, "desktop"})
		.descr("fullscreen: off, on, desktop");
	
	P_INT(target_fps, 1000)
		.descr("frames per second, rendering. 0 is native refresh rate");
	
	P_ENUM(set_vsync, int, {-1, "dont_set"}, {0, "force_off"}, {1, "on"})
		.descr("vertical synchronization: dont_set, force_off, on");
	
#define FONT(NM, DESCR) \
	cs.emplace_back("font" #NM)\
	.vstr(font##NM##_path)\
	.vfloat(font##NM##_pt)\
	.descr(DESCR)
	
	FONT(, "primary font - filename and size");
	FONT(_dbg, "debug info etc");
	
	P_INT(font_supersample, 8, 1)
		.descr("font atlas upscale");
	
	P_FLOAT(cam_pp_shake_str)
		.descr("camera shake effect strength");
	
	P_ENUM(interp_depth, int, {0, "0"}, {2, "2"}, {3, "3"})
		.descr("interpolation depth (0, 2 or 3)");

	P_ENUM(aal_type, AAL_Type, {AAL_OldFuzzy, "old_fuzzy"}, {AAL_CrispGlow, "crisp_glow"}, {AAL_Clear, "clear"})
		.descr("glowing lines type: old_fuzzy, crisp_glow, clear");
	
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
	wnd_size_max = true;
	fscreen = 0;
	target_fps = 0;
	set_vsync = 1;
	
	font_path = "data/Inconsolata-Bold.ttf";
	font_dbg_path = "data/Inconsolata.otf";
	font_pt = 16;
	font_dbg_pt = 16;
	font_supersample = 2;
	
	cam_pp_shake_str = 0.007;
	interp_depth = 3;
	aal_type = AAL_OldFuzzy;
	
	cursor_info_flags = 1;
	plr_status_blink = true;
	plr_status_flare = true;
	
	trigger_cbs();
}
void AppSettings::trigger_cbs()
{
	for (auto& cb : cbs) if (cb) cb();
}
RAII_Guard AppSettings::add_cb(std::function<void()> cb, bool call_now)
{
	size_t i=0;
	for (; i<cbs.size(); ++i) {
		if (!cbs[i])
			break;
	}
	if (i == cbs.size()) cbs.emplace_back();
	cbs[i] = std::move(cb);
	if (call_now) cbs[i]();
	return RAII_Guard([i] {AppSettings::get_mut().cbs[i] = {};});
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
