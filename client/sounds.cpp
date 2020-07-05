#include <atomic>
#include <filesystem>
#include <future>
#include <mutex>
#include <unordered_set>
#include <box2d/b2_dynamic_tree.h>
#include <box2d/b2_chain_shape.h>
#include <box2d/b2_edge_shape.h>
#include "client/presenter.hpp"
#include "core/hard_paths.hpp"
#include "core/settings.hpp"
#include "core/vig.hpp"
#include "game/game_core.hpp"
#include "utils/noise.hpp"
#include "utils/res_audio.hpp"
#include "utils/tokenread.hpp"
#include "vaslib/vas_containers.hpp"
#include "vaslib/vas_log.hpp"
#include "sounds.hpp"

const float speed_of_sound = 300; // cold, thin air - so lower than 340
static float db_to_lin(float db) {
	return std::pow(10, db/10);
}
static float sound_rtt(float dist) {
	return dist * 2 / speed_of_sound;
}

const TimeSpan upd_period_all = TimeSpan::ms(20); // how often ALL sounds are updated
const TimeSpan upd_period_one = TimeSpan::ms(100); // how often SINGLE sound is updated

const float dist_cull_radius = 120; // sounds further than that are immediatly stopped
const float dist_cull_radius_squ = dist_cull_radius * dist_cull_radius;

const float lowpass_vertical_pan = 0.5; // how much vertical panning affects filtering
const float lowpass_cutoff = 300; // Hz
// filter function: y += alpha * (x - y)

const float wall_dist_incr = 4; // additional increase in distance per wall meter
const float wall_pass_incr = 0.5 / GameConst::cell_size; // lowpass increase per wall meter

const float reverb_max_dist = 5; // max distance
const float reverb_dist_delta = 1; // distance between presets
const float reverb_line_maxvol = 1; // max volume of single line
const float reverb_vol_max = 0.7; // max effect volume
const float reverb_raycast_delta = GameConst::cell_size; // how far apart rays can be, perimiter meters, max distance
const int reverb_lines = 3; // 0 disables effect
const float reverb_t_thr = 0.01; // effect is disabled if 't_reverb' is lower
const float reverb_min_percentage = 0.85; // of rays hitting walls when effect should be max

const float pan_dist_full = 20; // full panning active if distance is higher
const float pan_max_k = 0.9; // max panning value

const float chan_vol_max = db_to_lin(-6); // mix headroom
const float rndvol_0 = db_to_lin(-2), rndvol_1 = 1; // volume randomization bounds

const TimeSpan sound_stopfade = TimeSpan::ms(200); // fade on leaving visibility range or being blocked
const TimeSpan sound_fadein = TimeSpan::ms(25); // time to fade on entering visibility range
const float max_amp_decrease = upd_period_one / sound_stopfade; // max amplitude decrease per update step

const TimeSpan music_crossfade = TimeSpan::seconds(5); // time to switch tracks
const TimeSpan music_pause_fade = TimeSpan::seconds(1.2); // time to fully change internal music volume

const TimeSpan limiter_pause = TimeSpan::seconds(2); // time for which multiplier only increased
const float limiter_pause_incr = 0.4 / limiter_pause.seconds(); // increment of multiplier when paused
const TimeSpan limiter_restore = TimeSpan::seconds(5); // time to bring multiplier back to 1.0

using samplen_t = int; ///< Length in samples



struct SoundInfo;
struct SoundData
{
	std::vector<int16_t> d; // always contains at least two samples; last sample is always same as first
	samplen_t len = 0;      //  and it's not counted in len
	int nchn = 1; // number of channels - 1 or 2
	bool is_loop = true;
	float reverb_offset; // ensures delayed position can't be negative; calculated by SoundEngine
};
struct SoundInfo
{
	std::vector<SoundData> data; // begin, {loop variation}, end. Always at least one element
	bool was_in_config = false; // for error report only
	
	bool is_ui = false;
	bool random_vol = false;
	bool randomized = false; // speed
	std::pair<float, float> spd_mut = {1, 1};
	std::pair<float, float> spd_rnd = {1, 1};
	float volume = 1;
	float max_dist = dist_cull_radius;
	float max_dist_squ = dist_cull_radius_squ; // threshold, square
	
	bool ok() const {return !data.empty();}
};
static std::vector<SoundInfo> load_sounds(int sample_rate, std::unordered_set<std::string>* dbg_fns = nullptr)
{
	TimeSpan time0 = TimeSpan::current();
	
	std::vector<SoundInfo> info;
	info.resize(SND_TOTAL_COUNT_INTERNAL);
	
	TokenReader tkr;
	std::string s_tkr = tkr.reset_file(HARDPATH_SOUND_LIST);
	if (s_tkr.empty())
		THROW_FMTSTR("load_sounds() failed to read config");
	
	try {
		while (!tkr.ended())
		{
			int id = -1;
			if (false) {}
#define X(a)\
	else if (tkr.is(#a)) id = a;
			SOUND_ID_X_LIST
#undef X
			if (id == -1)
				THROW_FMTSTR("unknown name/option: {}", tkr.raw());
			
			auto& in = info[id];
			if (in.ok())
				THROW_FMTSTR("duplicate: {}", get_name(static_cast<SoundId>(id)));
			in.was_in_config = true;
			
			auto subload = [&] {
				auto& d = in.data.emplace_back();
				auto name = tkr.str();
				if (name != "!") {
					auto fname = std::string(HARDPATH_SOUNDS_PREFIX) += name;
					auto ld = AudioSource::load(fname.c_str(), sample_rate);
					d.d = std::move(ld.data);
					d.nchn = ld.channels;
					d.len = d.d.size() / ld.channels;
				}
				if (d.d.empty()) {
					d.len = 1; // easy way to prevent division by zero
					d.d.push_back(0);
				}
				if (dbg_fns) dbg_fns->emplace(std::string(name));
				
				// add looped sample
				if (d.nchn == 1) {
					d.d.push_back(d.d[0]);
				}
				else {
					d.d.push_back(d.d[0]);
					d.d.push_back(d.d[1]);
				}
			};
			subload();
			
			while (!tkr.raw().empty())
			{
				if		(tkr.is("v") || tkr.is("vol"))  in.volume = tkr.num();
				else if (tkr.is("d") || tkr.is("dist")) {
					in.max_dist = tkr.num();
					in.max_dist_squ = in.max_dist * in.max_dist;
				}
				else if (tkr.is("rnd")) {
					in.randomized = true;
					in.spd_rnd.first  = tkr.num();
					in.spd_rnd.second = tkr.num();
				}
				else if (tkr.is("spdmut")) {
					in.spd_mut.first  = tkr.num();
					in.spd_mut.second = tkr.num();
				}
				else if (tkr.is("ui")) in.is_ui = true;
				else if (tkr.is("sub")) subload();
				else if (tkr.is("rndvol")) in.random_vol = true;
				else if (tkr.is("norndvol")) in.random_vol = false;
				else break;
			}
			
			in.data.front().is_loop = in.data.size() > 1;
			in.data.back().is_loop = false;
		}
	}
	catch (std::exception& e) {
		auto p = tkr.calc_position();
		THROW_FMTSTR("load_sounds() failed to read config - {} [at {}:{}]", e.what(), p.first, p.second);
	}
	
	int ok = 0; // ignore SND_NONE
	for (size_t i=1; i<info.size(); ++i) {
		if (info[i].ok()) ++ok;
		else if (!info[i].was_in_config)
			VLOGE("load_sounds() no {}", get_name(static_cast<SoundId>(i)));
		else
			VLOGE("load_sounds() failed {}", get_name(static_cast<SoundId>(i)));
	}
	if (ok != SND_TOTAL_COUNT_INTERNAL - 1)
		VLOGW("load_sounds() only {} of {} were loaded", ok, SND_TOTAL_COUNT_INTERNAL - 1);
	
	VLOGI("load_sounds() in {:.3f} seconds", (TimeSpan::current() - time0).seconds());
	return info;
}
static const char *name_array[] = {
#define X(a) #a,
	SOUND_ID_X_LIST
#undef X
    "<ERROR>"
};
const char *get_name(SoundId id) {
	return name_array[id];
}



SoundPlayParams& SoundPlayParams::_pos(vec2fp pos) {
	this->pos = pos;
	return *this;
}
SoundPlayParams& SoundPlayParams::_target(EntityIndex target) {
	this->target = target;
	return *this;
}
SoundPlayParams& SoundPlayParams::_t(float t) {
	this->t = t;
	return *this;
}
SoundPlayParams& SoundPlayParams::_period(TimeSpan t) {
	this->loop_period = t;
	return *this;
}
SoundPlayParams& SoundPlayParams::_volume(float t) {
	this->volume = t;
	return *this;
}



SoundObj::SoundObj(SoundObj&& b) noexcept
{
	id = b.id;
	b.id = {};
}
SoundObj& SoundObj::operator=(SoundObj&& b) noexcept
{
	stop();
	id = b.id;
	b.id = {};
	return *this;
}
void SoundObj::update(const SoundPlayParams& pars)
{
	if (auto p = SoundEngine::get())
		id = p->play(id, pars, true);
}
void SoundObj::update(Entity& ent, SoundPlayParams pars)
{
	pars.pos = ent.get_pos();
	pars.target = ent.index;
	update(pars);
}
void SoundObj::stop()
{
	if (id) {
		SoundEngine::get()->stop(id);
		id = {};
	}
}



struct SndEngMusic
{
	// if nothing played (by autoselect), cur_music is -1
	enum {
		SUB_PEACE,   // if no battle for some time (tmo_ambient_to_peace)
		             // ...or long battle (tmo_longbattle) finished
		SUB_AMBIENT, // other cases then there is no battle
		SUB_LIGHT, // played if any enemies are in combat mode - and escalates
		SUB_HEAVY, // ...to the next level if enough time passed (tmo_escalation)
		SUB_EPIC,  // ...or more enemies appear
		SUB__TOTAL_COUNT
	};
	struct Track {
		std::string fn; // filename
		int subsong = -1; // subsong index, if any (for tracker music)
		explicit operator bool() const {return !fn.empty();}
	};
	struct Group {
		// tracks are variations on the same theme
		std::array<Track, SUB__TOTAL_COUNT> ts = {};
		bool single = true; ///< All tracks are the same file
	};
	struct Order {
		std::vector<int> is; // groups indices; never empty
		size_t i = 0; // current index into 'is'
		
		void shuffle(bool first = false) {
			if (first) rnd_stat().shuffle(is.begin() + 1, is.end());
			else {
				int last = is.back();
				rnd_stat().shuffle(is);
				if (is.size() > 1 && is[0] == last)
					std::swap(is[0], is[rnd_stat().range_index(is.size(), 1)]);
			}
		}
	};
	
	static constexpr TimeSpan tmo_ambient_to_peace = TimeSpan::seconds(120);
	static constexpr TimeSpan tmo_battle_to_ambient = TimeSpan::seconds(6);
	static constexpr TimeSpan tmo_longbattle = TimeSpan::seconds(120);
	static constexpr TimeSpan tmo_escalation = TimeSpan::seconds(120);
	static constexpr TimeSpan track_switch_max = TimeSpan::seconds(8*60); // 100% to switch
	static constexpr TimeSpan track_switch_min = TimeSpan::seconds(3*60); // 0% to switch
	
	SoundEngine::MusControl state = SoundEngine::MUSC_NO_AUTO;
	std::vector<Group> groups;
	std::unordered_map<std::string, std::string> aliases;
	
	std::array<Order, SUB__TOTAL_COUNT> search;
	int group = 0;
	int cur_music = -1; // subtrack index or -1
	TimeSpan track_start; // since then this group is playing
	std::optional<TimeSpan> switch_at; // time at which change track group
	
	// GameCore time
	TimeSpan last_peace = TimeSpan::seconds(0);
	TimeSpan last_light = TimeSpan::seconds(-1000);
	TimeSpan last_heavy = TimeSpan::seconds(-1000);
	bool long_battle = false;
	
	void load();
	void step(SoundEngine& snd, TimeSpan now);
};
void SndEngMusic::load()
{
	TokenReader tkr;
	std::string s_tkr = tkr.reset_file(HARDPATH_MUSIC_LIST);
	if (s_tkr.empty())
		THROW_FMTSTR("SndEngMusic::load() failed to read config");
	
	try {
		auto get_type = [](auto& str) {
			if		(str == "peace")   return SUB_PEACE;
			else if (str == "ambient") return SUB_AMBIENT;
			else if (str == "light")   return SUB_LIGHT;
			else if (str == "heavy")   return SUB_HEAVY;
			else if (str == "epic")    return SUB_EPIC;
			THROW_FMTSTR("Unknown subtrack ID: {}", str);
		};
		while (!tkr.ended())
		{			
			auto str = tkr.str();
			if (str == "group")
			{
				auto& gr = groups.emplace_back();
				while (!tkr.ended())
				{
					auto str = tkr.str();
					if (str == "@") break;
					else {
						int i = get_type(str);
						while (!tkr.ended()) {
							auto str = tkr.str();
							if (str == "@") break;
							else {
								if (gr.ts[i]) {
									THROW_FMTSTR("Only one track per type! (group: {}, filename: \"{}\")",
									             groups.size() - 1, str);
								}
								gr.ts[i].fn = str;
							}
						}
					}
				}
				
				// check if not single
				std::string_view f0;
				for (auto& t : gr.ts) {
					if (f0.empty()) f0 = t.fn;
					else if (f0 != t.fn) gr.single = false;
				}
			}
			else if (str == "single")
			{
				auto& gr = groups.emplace_back();
				auto fn = std::string(tkr.str());
				while (!tkr.ended()) {
					auto str = tkr.str();
					if (str == "@") break;
					else {
						int i = get_type(str);
						if (gr.ts[i]) {
							THROW_FMTSTR("Only one track per type! (group: {}, filename: \"{}\")",
							             groups.size() - 1, str);
						}
						gr.ts[i].fn = fn;
					}
				}
			}
			else if (str == "alias")
			{
				auto a = tkr.str();
				auto b = tkr.str();
				aliases.emplace(std::move(a), std::move(b));
			}
			else THROW_FMTSTR("Invalid token: {}", str);
		}
	}
	catch (std::exception& e) {
		auto p = tkr.calc_position();
		THROW_FMTSTR("SndEngMusic::load() failed to read config - {} [at {}:{}]", e.what(), p.first, p.second);
	}
	
	for (size_t gi=0; gi<groups.size(); ++gi) {
		for (int i=0; i<SUB__TOTAL_COUNT; ++i) {
			if (groups[gi].ts[i])
				search[i].is.push_back(gi);
		}
	}
	for (auto& m : search) {
		if (m.is.empty()) THROW_FMTSTR("SndEngMusic::load() failed - music not specified for all modes");
		m.shuffle(true);
	}
}
void SndEngMusic::step(SoundEngine& snd, TimeSpan now)
{
	if (state == SoundEngine::MUSC_NO_AUTO) {
		cur_music = -1;
		return;
	}
	
	// find what should be played now
	int sel = cur_music;
	if (state == SoundEngine::MUSC_AMBIENT)
	{
		last_peace = now;
		if		(now - std::max(last_light, last_heavy) > tmo_ambient_to_peace) sel = SUB_PEACE;
		else if (now - std::max(last_light, last_heavy) > tmo_battle_to_ambient) {
			sel = long_battle ? SUB_PEACE : SUB_AMBIENT;
		}
	}
	else
	{
		long_battle = (now - last_peace > tmo_longbattle);
		if (state == SoundEngine::MUSC_EPIC) {
			last_heavy = now;
			sel = SUB_EPIC;
		}
		else if (state == SoundEngine::MUSC_HEAVY) {
			last_heavy = now;
			sel = std::max<int>(sel, SUB_HEAVY);
			if (now - last_light > tmo_escalation) sel = SUB_EPIC;
		}
		else {
			last_light = now;
			sel = std::max<int>(sel, SUB_LIGHT);
			if (now - last_peace > tmo_escalation) sel = std::max<int>(sel, SUB_HEAVY);
		}
	}
	if (cur_music == -1) {
		sel = std::max(0, sel);
		track_start = TimeSpan::current();
	}
	
	auto new_group = [&]{
		auto& s = search[sel];
		group = s.is[s.i];
		VLOGV("Now playing: {}", groups[s.is[s.i]].ts[sel].fn);
		track_start = now;
		if (++s.i == s.is.size()) {
			s.i = 0;
			s.shuffle();
		}
	};
	auto play = [&]{
		auto& gr = groups[group];
		auto& tr = gr.ts[sel];
		snd.music(tr.fn.c_str(), tr.subsong, !gr.single, false);
	};
	if (cur_music != sel)
	{
		if (sel == SUB_PEACE || sel == SUB_AMBIENT) {
			if (!switch_at) switch_at = track_start + lerp(track_switch_min, track_switch_max, rnd_stat().range_n());
			if (now > *switch_at) new_group();
		}
		else switch_at = {};
		
		if (!groups[group].ts[sel]) // no suitable track in current group
			new_group();
		
		cur_music = sel;
		play();
	}
	else if (!snd.has_music())
	{
		if (track_start.is_positive()) new_group(); // use default group when first track in current engine run is played
		play();
	}
}
int SoundEngine::check_unused_sounds()
{
	int ret = 0;
	enum {
		R_UNUSED_FILE = 1,
		R_MUSIC = 2,
		R_CONFIG = 4
	};
	
	try {
		std::unordered_set<std::string> fns;
		fns.emplace("LIST");
		fns.emplace("music");
		VLOGI("Checking sounds...");
		auto info = load_sounds(22050, &fns);
		size_t bytes = 0;
		for (int i=0; i<SND_TOTAL_COUNT_INTERNAL; ++i) {
			bool any = false;
			for (auto& d : info[i].data) {if (d.len > 2) {any = true; bytes += 2 * d.d.capacity();}}
			if (!any) VLOGW("Silence {}", get_name((SoundId)i));
		}
		for (auto& e : std::filesystem::directory_iterator(HARDPATH_SOUNDS_PREFIX)) {
			if (fns.end() == fns.find(e.path().filename().replace_extension().u8string())) {
				ret |= R_UNUSED_FILE;
				VLOGW("Unused file '{}'", e.path().u8string());
			}
		}
		VLOGI("Check finished");
		
		VLOGI("Checking music...");
		SndEngMusic mc;
		mc.load();
		fns.clear();
		for (auto& tr : mc.groups) {
			for (auto& t : tr.ts)
				fns.emplace(t.fn);
		}
		for (auto& as : mc.aliases) {
			fns.emplace(as.second);
		}
		for (auto& name : fns) {
			std::string s = std::string(HARDPATH_MUSIC_PREFIX) + name;
			auto p = AudioSource::open_stream(s.c_str(), 22050);
			if (!p) ret |= R_MUSIC;
			delete p;
		}
		fns.emplace("LIST");
		fns.emplace("LICENSE");
		for (auto& e : std::filesystem::directory_iterator(HARDPATH_MUSIC_PREFIX)) {
			if (fns.end() == fns.find(e.path().filename().replace_extension().u8string())) {
				ret |= R_UNUSED_FILE;
				VLOGW("Unused file '{}'", e.path().u8string());
			}
		}
		VLOGI("Check finished");
	}
	catch (std::exception& e) {
		VLOGE("Exception: {}", e.what());
		ret |= R_CONFIG;
	}
	
	return ret;
}



struct AudioOutputDevice {
	virtual void set_pause(bool on) = 0;
	virtual ~AudioOutputDevice() = default;
};
class SoundEngine_Impl;
static AudioOutputDevice* audiodev_profile(SoundEngine_Impl* u, int n_samples);
static AudioOutputDevice* audiodev_sdl2(SoundEngine_Impl* u, const char *api_name, const char *dev_name, int sample_rate, int n_samples);
static AudioOutputDevice* audiodev_portaudio(SoundEngine_Impl* u, const char *api_name, const char *dev_name, int sample_rate, int n_samples);



class SoundEngine_Impl : public SoundEngine
{
public:
	// general
	std::atomic<float> g_vol = 1;
	std::atomic<float> g_sfx_vol = 1;
	std::atomic<bool> g_ui_only = true;
	vec2fp g_lstr_pos = {}; // listener position
	
	std::unique_ptr<AudioOutputDevice> device;
	int sample_rate;
	
	bool reverb_enabled;
	float lowpass_alpha;
	float lowpass_amp;
	
	// resources
	std::vector<SoundInfo> snd_res;
	
	using ReverbPars = std::array<std::pair<float, float>, reverb_lines>; // delta, amp
	std::vector<ReverbPars> rev_pars;
	ReverbPars rev_zero = {};
	
	// channels
	struct ChannelFrame
	{
		float kpan[2] = {0, 0}; // channel volume
		float lowpass = 0; // wet coeff
		float t_pitch = 1; // speed multiplier
		float t_reverb = 0;
		ReverbPars* rev = nullptr;
		
		void diff(const ChannelFrame& prev, const ChannelFrame& next, float time_k) {
			for (int i=0; i<2; ++i) kpan[i] = (next.kpan[i] - prev.kpan[i]) * time_k;
			lowpass  = (next.lowpass  - prev.lowpass)  * time_k;
			t_reverb = (next.t_reverb - prev.t_reverb) * time_k;
			t_pitch  = (next.t_pitch  - prev.t_pitch)  * time_k;
		}
		void add(const ChannelFrame& f) {
			for (int i=0; i<2; ++i) kpan[i] += f.kpan[i];
			lowpass  += f.lowpass;
			t_reverb += f.t_reverb;
			t_pitch  += f.t_pitch;
		}
	};
	struct Channel
	{
		enum {
			IS_OBJ = 1, // is allocated by SoundObj (and therefore looped)
			IS_ACTIVE = 2, // is processed
			WAS_ACTIVE = 4,
			STOP = 8, // stop when frame ends
			IS_NEW = 16, // just added, not yet played
			IS_STATIC = 32 // never moves - shouldn't be updated (only reverb)
		};
		
		// params
		SoundInfo* p_snd = nullptr;
		std::optional<vec2fp> w_pos; // position
		EntityIndex target; // anchor, if any
		float p_pitch; // playback speed multiplier from 't' parameter
		float volume; // already multiplied by p_snd
		samplen_t loop_period; // used to pad loops with silence if sound itself too short
		
		// playback
		int subsrc = 0;
		float pb_pos = 0;
		samplen_t silence_left = 0;
		samplen_t frame_left = 0;
		samplen_t frame_length = 0; // last one - used for interpolation
		ChannelFrame cur, next, mod;
		
		// effects data
		float lpass[2] = {0,0}; // lowpass filter memory
		float dopp_mul = 1; // doppler pitch (speed) multiplier
		
		// control
		int32 body; // cull sensor - node index for detect tree
		uint32_t upd_at; // update step or DONT_UPDATE (-1)
		uint8_t flags = IS_ACTIVE | IS_NEW;
		float g_amp = 0; // used only to limit decrease
		
		explicit operator bool() const {return p_snd;}
		bool is_proc() const {return (flags & IS_ACTIVE) || frame_left;}
	};
	
	// control
	std::mutex chan_lock;
	SparseArray<Channel> chans;
	SndEngMusic musc;
	
	b2DynamicTree snd_detect; // detect active (non-culled) channels
	b2DynamicTree wall_detect;
	std::vector<b2EdgeShape> wall_shapes;
	std::vector<int32> wall_nodeindices;
	
	samplen_t update_accum = 0;
	uint32_t update_step = 0;
	static constexpr auto DONT_UPDATE = std::numeric_limits<uint32_t>::max();
	
	// preallocated data
	std::vector<float> mix_buffer;
	std::vector<int16_t> load_buffer;
	std::vector<float> cast_dists;
	
	// music
	std::mutex music_lock;
	std::string mus_prev_name;
	std::atomic<float> g_mus_vol = 1;
	std::future<void> mus_load;
	std::unique_ptr<AudioSource> mus_src;
	int mus_pos = 0;
	
	std::unique_ptr<AudioSource> musold_src; // previous track - for crossfade
	int musold_pos;
	samplen_t musold_left = 0;
	samplen_t mus_left_full = 0;
	
	float mus_vol_tar = 1, mus_vol_cur = 1; // ui lock fade
	bool mus_loop;
	
	// debug
	TimeSpan callback_len;
	std::array<std::pair<TimeSpan, TimeSpan>, 6> dbg_sum = {}; // sum and max
	size_t dbg_count = 0, dbg_count_upd = 0;
	
	std::vector<TimeSpan> dbg_times;
	size_t i_dbg_times = 0;
	RAII_Guard dbg_menu;
	
	// simple limiter
	float limit_mul = 0.999;
	samplen_t limit_pause = 0;
	
	
	
	static b2AABB mk_aabb(vec2fp ctr, float radius) {
		return {{ctr.x - radius, ctr.y - radius}, {ctr.x + radius, ctr.y + radius}};
	}
	
	SoundEngine_Impl(bool profile_mode)
	{
		// init
		
		const char *api_name = AppSettings::get().audio_api.c_str();
		if (!std::strlen(api_name)) api_name = nullptr;
		
		const char *dev_name = AppSettings::get().audio_device.c_str();
		if (!std::strlen(dev_name)) dev_name = nullptr;
		
		int n_samples = AppSettings::get().audio_samples;
		sample_rate = AppSettings::get().audio_rate;
		callback_len = TimeSpan::seconds(double(n_samples) / sample_rate);
		
		reverb_enabled = AppSettings::get().use_audio_reverb && reverb_lines > 0;
		// magic!
		lowpass_alpha = 1 - std::exp(-(1./sample_rate) / (1./lowpass_cutoff));
		lowpass_amp = 1.f; // 1 / std::sqrt(lowpass_alpha);
		
		VLOGI("Audio: \"{}\" - \"{}\"", api_name? api_name : "(default)", dev_name? dev_name : "(default)");
		VLOGI("Audio: {} Hz, {} samples, {:.3f}ms frame",
			  sample_rate, n_samples, callback_len.micro() / 1000.f);
		
		if (profile_mode) device.reset(audiodev_profile(this, n_samples));
		else {
			if (AppSettings::get().use_portaudio) {
				try {
					device.reset(audiodev_portaudio(this, api_name, dev_name, sample_rate, n_samples));
				}
				catch (std::exception& e) {
					VLOGE("PortAudio init failed: {}", e.what());
					VLOGW("Using SDL2 audio as fallback");
					device.reset(audiodev_sdl2(this, api_name, dev_name, sample_rate, n_samples));
				}
			}
			else device.reset(audiodev_sdl2(this, api_name, dev_name, sample_rate, n_samples));
		}
		
		// setup
		
		set_master_vol(AppSettings::get().audio_volume);
		set_sfx_vol(AppSettings::get().sfx_volume);
		set_music_vol(AppSettings::get().music_volume);
		
		mix_buffer.resize(n_samples * 2);
		load_buffer.resize(sample_rate * 2);
		
		// https://github.com/bdejong/musicdsp/blob/master/source/Effects/44-delay-time-calculation-for-reverberation.rst
		int rev_num = std::ceil( reverb_max_dist / reverb_dist_delta );
		rev_pars.resize(rev_num);
		for (int i=0; i<rev_num; ++i)
		{
			float dist = (i + 0.5f) * reverb_dist_delta;
			float t0 = sound_rtt(dist);
			float g0 = reverb_line_maxvol * (1 - dist / reverb_max_dist);
			float rev = -3 * t0 / std::log10(g0);
			
			for (int j=0; j<reverb_lines; ++j)
			{
				auto& p = rev_pars[i][j];
				float dt = t0 / std::pow(2, float(j) / reverb_lines);
				p.first = sample_rate * dt;
				p.second = std::pow(10, -((3*dt) / rev));
			}
		}
		
		snd_res = load_sounds(sample_rate);
		musc.load();
		
		float max_reverb_delay = rev_pars.back().front().first;
		VLOGV("max_reverb_delay = {:.3f} ms", 1000 * max_reverb_delay / sample_rate);
		for (auto& src : snd_res)
		for (auto& d : src.data) {
			int n = d.len / max_reverb_delay + 1;
			d.reverb_offset = n * d.len;
		}
		
		dbg_times.resize(TimeSpan::seconds(3) / callback_len);
		dbg_menu = vig_reg_menu(VigMenu::DebugRenderer, [this]
		{
			int an = 0;
			for (auto& c : chans) if (c.is_proc()) ++an;
			vig_label_a("Sound channels: {:2} <- {:3}\n", an, chans.existing_count());
			
			TimeSpan max, avg;
			for (auto& t : dbg_times) {
				max = std::max(max, t);
				avg += t;
			}
			vig_label_a("last 3 seconds: max {:3}, avg {:3}\n", int(max / callback_len * 100.), int(avg / callback_len * (100. / dbg_times.size())));
			
			vig_label_a("music {}, control {}, group {}, sel {}\n", has_music(), musc.state != MUSC_NO_AUTO, musc.group, musc.cur_music);
		});
		
		device->set_pause(false);
	}
	~SoundEngine_Impl()
	{
		// gracefully fade before shutdown
		TimeSpan exit_wait;
		set_ui_mode(UIM_SILENCE);
		while (true)
		{
			{	std::unique_lock l1(chan_lock);
				std::unique_lock l2(music_lock);
				int an = 0;
				for (auto& c : chans) if (c.flags & Channel::IS_ACTIVE) ++an;
				if (!an && ((!mus_src && !musold_src) || mus_vol_cur < 0.01))
					break;
				
				if (exit_wait > upd_period_all*2) {
					for (auto& c : chans) {
						if (c.flags & Channel::IS_OBJ) {
							VLOGE("SoundEngine:: SoundObjs exist on shutdown fade");
							break;
						}
					}
				}
				if (exit_wait > TimeSpan::seconds(5)) {
					VLOGE("SoundEngine:: shutdown fade failed");
					break;
				}
			}
			sleep(TimeSpan::ms(50));
			exit_wait += TimeSpan::ms(50);
		}
		VLOGI("Audio exit wait: {:.3f} seconds", exit_wait.seconds());
		
		device.reset();
		
		VLOGI("Audio thread times (avg, max) - {} samples:", dbg_count);
		for (int i=0; i<6; ++i) {
			const char *pref;
			size_t cnt = dbg_count;
			switch (i) {
			case 0: pref = "total"; break;
			case 1: pref = "music"; break;
			case 2: pref = "vis  "; cnt = dbg_count_upd; break;
			case 3: pref = "upd  "; cnt = dbg_count_upd; break;
			case 4: pref = "proc "; break;
			case 5: pref = "convt"; break;
			}
			auto sum = dbg_sum[i].first;
			auto max = dbg_sum[i].second;
			auto perc = [&](auto t) {return static_cast<int>(100 * (t / callback_len));};
			sum *= 1.f / cnt;
			VLOGI("  {}:  {:7}mks  {:3}%   {:7}mks  {:3}%", pref, sum.micro(), perc(sum), max.micro(), perc(max));
		}
	}
	void geom_static_add(const b2ChainShape& shp) override
	{
		std::unique_lock lock(chan_lock);
		for (int i=1; i<shp.m_count; ++i)
		{
			auto& ns = wall_shapes.emplace_back();
			ns.Set(shp.m_vertices[i-1], shp.m_vertices[i]);
			
			b2AABB aabb;
			b2Transform tr;
			tr.SetIdentity();
			ns.ComputeAABB(&aabb, tr, 0);
			int32 node = wall_detect.CreateProxy(aabb, reinterpret_cast<void*>(static_cast<intptr_t>(wall_shapes.size() - 1)));
			wall_nodeindices.push_back(node);
		}
	}
	void geom_static_clear() override
	{
		std::unique_lock lock(chan_lock);
		for (auto& i : wall_nodeindices) wall_detect.DestroyProxy(i);
		wall_shapes.clear();
		wall_nodeindices.clear();
	}
	float get_master_vol() override
	{
		return g_vol;
	}
	void set_master_vol(float vol) override
	{
		g_vol = std::min(vol, 0.999f);
	}
	float get_sfx_vol() override
	{
		return g_sfx_vol;
	}
	void set_sfx_vol(float vol) override
	{
		g_sfx_vol = std::min(vol, 0.999f);
	}
	float get_music_vol() override
	{
		return g_mus_vol;
	}
	void set_music_vol(float vol) override
	{
		g_mus_vol = std::min(vol, 0.999f);
	}
	void music(const char *name, int subtrack_index, bool loop, bool disable_musc) override
	{
		if (disable_musc) {
			music_control(MUSC_NO_AUTO);
			std::unique_lock lock(music_lock);
			mus_vol_tar = 1;
		}
		if (mus_load.valid()) mus_load.get();
		
		if (!name) {
			std::unique_lock lock(music_lock);
			if (mus_src) {
				musold_src = std::move(mus_src);
				musold_pos = mus_pos;
				musold_left = music_pause_fade.seconds() * sample_rate;
				mus_left_full = musold_left;
			}
			mus_src.reset();
			mus_pos = 0;
			return;
		}
		else if (mus_prev_name == name)
		{
			std::unique_lock lock(music_lock);
			if (mus_src) {
				if (subtrack_index != -1)
					mus_src->select_subsong(subtrack_index);
				return;
			}
		}
		
		mus_prev_name = name;
		mus_loop = loop;
		
		if (auto it = musc.aliases.find(name); it != musc.aliases.end()) name = it->second.c_str();
		std::string s = std::string(HARDPATH_MUSIC_PREFIX) + name;
		
		mus_load = std::async(std::launch::async, [this, s = std::move(s), subtrack_index]
		{
			set_this_thread_name("music load");
			auto src = AudioSource::open_stream(s.data(), sample_rate);
			if (!src) VLOGE("SoundEngine::music() failed (\"{}\")", s);
			
			std::unique_lock lock(music_lock);
			if (mus_src) {
				musold_src = std::move(mus_src);
				musold_pos = mus_pos;
				musold_left = music_crossfade.seconds() * sample_rate;
				mus_left_full = musold_left;
			}
			mus_src.reset(src);
			mus_pos = 0;
			
			if (subtrack_index != -1 && mus_src)
				mus_src->select_subsong(subtrack_index);
		});
	}
	bool has_music() override
	{
		std::unique_lock lock(music_lock);
		return mus_src || musold_src;
	}
	void music_control(MusControl state) override
	{
		std::unique_lock lock(chan_lock);
		musc.state = state;
	}
	SoundObj::Id play(SoundObj::Id i_obj, const SoundPlayParams& pp, bool continious) override
	{
		if (!continious && (g_vol < 1e-5 || g_sfx_vol < 1e-5))
			return {};
		
		std::unique_lock lock(chan_lock);
		auto& info = snd_res[pp.id];
		
		if (!info.ok()) {
			stop(i_obj, false);
			return {};
		}
		if (!continious && pp.pos && pp.pos->dist_squ(g_lstr_pos) > dist_cull_radius_squ) {
			stop(i_obj, false);
			return {};
		}
		
		if (i_obj && chans[i_obj.i].p_snd != &info) {
			stop(i_obj, false);
			i_obj = {};
		}
		
		bool is_new = !i_obj;
		if (is_new) {
			i_obj.i = chans.emplace_new();
		}
		Channel& ch = chans[i_obj.i];
		
		ch.p_snd = &info;
		ch.w_pos = pp.pos;
		ch.target = pp.target;
		ch.p_pitch = pp.t ? lerp(info.spd_mut.first, info.spd_mut.second, *pp.t)
		                  : (is_new && info.randomized ? lerp(info.spd_rnd.first, info.spd_rnd.second, rnd_stat().range_n())
		                                               : 1);
		ch.volume = (info.random_vol ? rnd_stat().range(rndvol_0, rndvol_1) : 1) * clampf_n(pp.volume) * info.volume;
		ch.loop_period = pp.loop_period.seconds() * sample_rate;
		
		if (is_new) {
			if (!ch.w_pos && !ch.target) ch.body = b2_nullNode;
			else {
				if (!ch.w_pos) ch.w_pos = vec2fp{-10000, -10000};
				ch.body = snd_detect.CreateProxy(mk_aabb(*ch.w_pos, 1),
				                                 reinterpret_cast<void*>(static_cast<intptr_t>(i_obj.i)));
			}
		}
		else { // restart non-looped sounds
			ch.flags |= Channel::IS_ACTIVE;
			ch.flags &= ~Channel::STOP;
		}
		
		if (continious) ch.flags |= Channel::IS_OBJ;
		else if (!ch.target) ch.flags |= Channel::IS_STATIC;
		ch.upd_at = update_step;
		
		return i_obj;
	}
	void stop(SoundObj::Id i_obj, bool do_lock) override
	{
		if (!i_obj) return;
		std::unique_lock<std::mutex> lock;
		if (do_lock) lock = std::unique_lock(chan_lock);
		int i = i_obj.i;
		if (chans[i].flags & Channel::IS_ACTIVE) chans[i].flags &= ~Channel::IS_OBJ;
		else free_channel(i);
	}
	void sync(GameCore& core, vec2fp lstr_pos) override
	{
		if (debug_draw)
		{
			// (╯°□°)╯彡┻━┻
			struct Info {
				SoundInfo* snd;
				int sub;
				std::optional<vec2fp> pos;
			};
			std::vector<Info> info;
			info.reserve(chans.size());
			{
				std::unique_lock l2(chan_lock);
				for (auto& vc : chans) {
					auto& in = info.emplace_back();
					in.snd = vc.p_snd;
					in.sub = (vc.flags & Channel::IS_ACTIVE)? vc.subsrc : -1;
					in.pos = vc.w_pos;
				}
			}
			for (auto& in : info) {
				vec2fp pos = in.pos.value_or(lstr_pos);
				size_t i_snd = std::distance(snd_res.data(), in.snd);
				if (in.sub != -1) {
					int st = in.sub == 0 ? 0 : (in.sub != (int) in.snd->data.size() - 1 ? 1 : 2);
					GamePresenter::get()->dbg_text(pos, FMT_FORMAT("{}[{}]", name_array[i_snd], st), 0xffff'ffa0);
				}
				else {
					GamePresenter::get()->dbg_text(pos, FMT_FORMAT("{}", name_array[i_snd]), 0x8080'80a0);
				}
			}
		}
		
		std::unique_lock l2(chan_lock);
		vec2fp lstr_vel = (lstr_pos - g_lstr_pos) * core.time_mul;
		g_lstr_pos = lstr_pos;
		
		for (auto& c : chans) {
			vec2fp vel = {};
			
			if (c && c.target)
			if (auto e = core.valid_ent(c.target)) {
				vec2fp n_pos = e->get_pos();
				vec2fp dt = n_pos - *c.w_pos;
				if (c.body != b2_nullNode) snd_detect.MoveProxy(c.body, mk_aabb(*c.w_pos, 1), {dt.x, dt.y});
				vel = dt * core.time_mul;
				c.w_pos = n_pos;
			}
			
			if (c.w_pos) {
				vec2fp dt = *c.w_pos - lstr_pos;
				float dist = dt.fastlen();
				c.dopp_mul = (speed_of_sound + dot(lstr_vel, dt) / dist) / (speed_of_sound + dot(vel, dt) / dist);
			}
		}
		
		if (g_mus_vol > 0.01)
			musc.step(*this, core.get_step_time());
	}
	void set_ui_mode(UI_Mode mode) override
	{
		std::unique_lock lock1(chan_lock);
		std::unique_lock lock2(music_lock);
		g_ui_only = (mode != UIM_OFF);
		mus_vol_tar = (mode == UIM_SILENCE) ? 0 : 1;
		if (!mus_src && !musold_src) mus_vol_cur = mus_vol_tar;
	}
	
	
	
	void free_channel(int i)
	{
		if (chans[i].body != b2_nullNode) snd_detect.DestroyProxy(chans[i].body);
		chans.free_and_reset(i);
	}
	void stop_channel(int i)
	{
		auto& vc = chans[i];
		vc.flags &= ~(Channel::IS_ACTIVE | Channel::WAS_ACTIVE);
		vc.flags |= Channel::STOP;
		vc.upd_at = DONT_UPDATE;
		
		if (vc.flags & Channel::IS_NEW) {
			if (vc.flags & Channel::IS_OBJ) {
				auto& ds = vc.p_snd->data;
				if (ds[0].is_loop) vc.subsrc = rnd_stat().range_index(ds.size() - 1, 1);
			}
			else free_channel(i);
			return;
		}
		
		vc.frame_left = sound_stopfade.seconds() * sample_rate;
		vc.next = vc.cur;
		for (int i=0; i<2; ++i) vc.next.kpan[i] = 0;
		vc.mod.diff(vc.cur, vc.next, 1.f / vc.frame_left);
	}
	
	void callback_music(const int outbuflen)
	{
		const float ZERO_VOL = 0.001;
		
		auto set_zero = [&](int i = 0) {
			if constexpr (std::numeric_limits<float>::is_iec559) {
				std::memset(mix_buffer.data() + i, 0, (outbuflen - i) * sizeof(float));
			}
			else {
				for (; i < outbuflen; ++i)
					mix_buffer[i] = 0;
			}
		};
		
		std::unique_lock lock(music_lock);
		if ((!mus_src && !musold_src) || (mus_vol_cur <= ZERO_VOL && mus_vol_tar <= ZERO_VOL)) {
			lock.unlock();
			set_zero();
			return;
		}
		
		float vol = g_mus_vol * mus_vol_cur;
		if (mus_src)
		{
			for (int i=0; i<outbuflen; )
			{
				int n = mus_src->read(load_buffer.data(), mus_pos, outbuflen - i);
				for (int j=0; j<n; ++j) {
					mix_buffer[i + j] = vol * load_buffer[j];
				}
				i += n;
				mus_pos += n;
				if (!n) {
					if (mus_loop) mus_pos = 0;
					else {
						mus_src.reset();
						set_zero(i);
						break;
					}
				}
			}
		}
		else {
			set_zero();
		}
		
		if (musold_left)
		{
			float t0 = float(musold_left) / mus_left_full;
			int len = std::min(musold_left, outbuflen/2);
			musold_left -= len;
			float t1 = float(musold_left) / mus_left_full;
			len *= 2;
			
			for (int i=0; i<len; ++i) {
				float t = 1.f - lerp(t0, t1, float(i) / len);
				mix_buffer[i] *= t;
			}
			for (int i=0; i<len; )
			{
				int n = musold_src->read(load_buffer.data(), musold_pos, len - i);
				for (int j=0; j<n; ++j) {
					float t = lerp(t0, t1, float(i + j) / len);
					mix_buffer[i + j] += t * vol * load_buffer[j];
				}
				i += n;
				musold_pos += n;
				if (!n) break; // don't loop
			}
			if (!musold_left)
				musold_src.reset();
		}
		if (mus_vol_cur != mus_vol_tar)
		{
			float v0 = mus_vol_cur;
			float vchx = music_pause_fade.seconds() * (outbuflen/2) / sample_rate;
			if (mus_vol_cur < mus_vol_tar) mus_vol_cur = std::min(mus_vol_tar, mus_vol_cur + vchx);
			else                           mus_vol_cur = std::max(mus_vol_tar, mus_vol_cur - vchx);
			
			for (int i=0; i<outbuflen; ++i) {
				mix_buffer[i] *= lerp(v0, mus_vol_cur, float(i) / outbuflen);
			}
			
			if (mus_vol_cur == mus_vol_tar && mus_vol_cur <= ZERO_VOL)
			{
				musold_left = 0;
				musold_src.reset();
			}
		}
	}
	void callback_visibility(const vec2fp lstr_pos)
	{
		for (auto& vc : chans) {
			if ((vc.flags & Channel::STOP) || !vc.w_pos) continue;
			bool was = (vc.flags & Channel::IS_ACTIVE);
			vc.flags &= ~(Channel::IS_ACTIVE | Channel::WAS_ACTIVE);
			if (was) vc.flags |= Channel::WAS_ACTIVE;
		}
		
		struct CB {
			SoundEngine_Impl& self;
			CB(SoundEngine_Impl& self): self(self) {}
			bool QueryCallback(int32 node) {
				int i = reinterpret_cast<intptr_t>(self.snd_detect.GetUserData(node));
				self.chans[i].flags |= Channel::IS_ACTIVE;
				return true;
			}
		};
		CB cb{*this};
		snd_detect.Query(&cb, mk_aabb(lstr_pos, dist_cull_radius));
		
		for (auto it = chans.begin(); it != chans.end(); ++it)
		{
			if (bool(it->flags & Channel::IS_ACTIVE) != bool(it->flags & Channel::WAS_ACTIVE))
			{
				if (it->flags & Channel::IS_ACTIVE) it->upd_at = update_step;
				else stop_channel(it.index());
			}
		}
	}
	void callback_update(const bool ui_only, const vec2fp lstr_pos, const samplen_t update_length)
	{
		const samplen_t frame_length = upd_period_one.seconds() * sample_rate;
		const samplen_t frame_fadein = std::min<samplen_t>(sound_fadein.seconds() * sample_rate, frame_length);
		
		const uint32_t upd_increment = [&]{
			auto n = std::max<int>(1, upd_period_all / upd_period_one);
			int inc = (update_accum / update_length) / n;
			return n * (inc + 1);
		}();
		const uint32_t step_1 = update_step + update_accum / update_length;
		
		auto raycast = [&](vec2fp a, vec2fp b, callable_ref<void(float fraction)> f){
			struct CB {
				SoundEngine_Impl& self;
				callable_ref<void(float)>* f;
				CB(SoundEngine_Impl& self): self(self) {}
				float RayCastCallback(const b2RayCastInput& input, int32 node) {
					int i = reinterpret_cast<intptr_t>(self.wall_detect.GetUserData(node));
					auto& shp = self.wall_shapes[i];
					b2RayCastOutput out;
					b2Transform tr;
					tr.SetIdentity();
					if (shp.RayCast(&out, input, tr, 0)) (*f)(out.fraction);
					return input.maxFraction;
				}
			};
			CB cb(*this);
			cb.f = &f;
			b2RayCastInput input;
			input.p1 = {a.x, a.y};
			input.p2 = {b.x, b.y};
			input.maxFraction = 1.f;
			wall_detect.RayCast(&cb, input);
		};
		
		for (auto it = chans.begin(); it != chans.end(); ++it)
		{
			if (it->upd_at >= step_1) continue;
			auto& vc = *it;
			
			if (ui_only && !vc.p_snd->is_ui) {
				stop_channel(it.index());
				continue;
			}
			
			//
			
			auto& frm = vc.next;
			vc.cur = frm;
			frm = ChannelFrame{};
			
			float kdist = 0; // relative distance (0 closest, 1 farthest)
			float wall = 1; // reverse wall factor (0 full wall, 1 no wall)
			float pan = 0, vpan = 0;
			
			const float min_dist = 1;
			if (vc.w_pos)
			{
				vec2fp dt = lstr_pos - *vc.w_pos;
				float dist = dt.len_squ();
				
				if (dist > vc.p_snd->max_dist_squ) {
					stop_channel(it.index());
					continue;
				}
				else if (dist > min_dist)
				{
					dist = 1.f / fast_invsqrt(dist);
					
					cast_dists.clear();
					raycast(lstr_pos, *vc.w_pos, [&](float f){
						cast_dists.push_back(f);
					});
					
					std::sort(cast_dists.begin(), cast_dists.end());
					for (size_t i=0; i + 1 < cast_dists.size(); i += 2)
					{
						float w = (cast_dists[i+1] - cast_dists[i]) * dist;
						wall -= wall_pass_incr * w;
						
						dist += wall_dist_incr * w;
						if (dist > vc.p_snd->max_dist)
							break;
					}
					if (dist > vc.p_snd->max_dist) {
						stop_channel(it.index());
						continue;
					}
					
					kdist = dist / vc.p_snd->max_dist; // linear decay
					wall = std::max(wall, 0.f);
					
					//
					
					float kxy = dot(dt / dist, vec2fp(1,0));
					pan = std::copysign(std::min(std::abs(kxy), pan_max_k), -kxy);
					vpan = std::copysign(1 - std::abs(kxy), -dt.y);
					
					float k = dist > pan_dist_full ? 1 : (dist - min_dist) / (pan_dist_full - min_dist);
					pan *= k;
					vpan *= k;
					
					//
					
					if (reverb_enabled && wall > 0.999f && (!(vc.flags & Channel::IS_STATIC) || (vc.flags & Channel::IS_NEW)))
					{
						float frac = 0;
						int n_rays = (2*M_PI * reverb_max_dist) / reverb_raycast_delta;
						int n_hits = 0;
						for (int i=0; i<n_rays; ++i)
						{
							float rot = 2*M_PI * i / n_rays;
							vec2fp dir = vec2fp(reverb_max_dist, 0).fastrotate(rot);
							bool was_hit = false;
							float minfrac = 1;
							raycast(*vc.w_pos, *vc.w_pos + dir, [&](float f){
								minfrac = std::min(minfrac, f);
								was_hit = true;
							});
							if (was_hit) {
								++n_hits;
								frac += minfrac;
							}
						}
						if (n_hits) {
							frac /= n_hits;
							frm.t_reverb = reverb_vol_max * (1 - frac * frac);
							frm.t_reverb *= std::min(1.f, n_hits / reverb_min_percentage / n_rays);
							int i = std::min<int>(frac * rev_pars.size(), rev_pars.size()-1);
							frm.rev = &rev_pars[i];
						}
						else {
							frm.t_reverb = 0;
							frm.rev = nullptr;
						}
					}
				}
			}
			
			// limit amplitude decrease per step
			vc.g_amp = std::max(chan_vol_max * (1 - kdist) * vc.volume, vc.g_amp - max_amp_decrease);
			        
			frm.lowpass = 1 - wall;
			frm.kpan[0] = frm.kpan[1] = vc.g_amp;
			
			auto cs = cossin_lut(M_PI_4 * (1 + pan));
			frm.kpan[0] *= M_SQRT2 * std::abs(cs.x);
			frm.kpan[1] *= M_SQRT2 * cs.y;
			frm.lowpass = clampf_n(frm.lowpass + vpan * lowpass_vertical_pan);
			
			//
			
			if (vc.flags & Channel::IS_NEW) vc.cur = frm;
			vc.flags &= ~(Channel::IS_NEW | Channel::STOP);
			vc.upd_at = update_step + upd_increment;
			
			vc.frame_length = (vc.flags & Channel::WAS_ACTIVE) ? frame_length : frame_fadein;
			vc.frame_left = vc.frame_length;
			vc.next.t_pitch = vc.p_pitch;
			vc.mod.diff(vc.cur, vc.next, 1.f / vc.frame_length);
			
			if (bool(vc.cur.rev) != bool(vc.next.rev)) {
				if (!vc.cur.rev) vc.cur.rev = &rev_zero;
				else vc.next.rev = &rev_zero;
			}
		}
	}
	template<int NC> void callback_process_loop(Channel& vc, int i_chan,
	                                            SoundData* src, int& si,
	                                            const int outbuflen, const float sfx_vol)
	{
		ChannelFrame& cur = vc.cur;
		ChannelFrame& mod = vc.mod;
		
		auto upd_silence = [&]{
			if (vc.silence_left > 0) {
				int n = std::min(vc.silence_left, outbuflen/2 - si);
				si += n;
				vc.silence_left -= n;
			}
		};
		auto newsub = [&]{
			vc.pb_pos = 0;
			auto n_src = &vc.p_snd->data[vc.subsrc];
			if (n_src->nchn == src->nchn) {
				src = n_src;
				return true;
			}
			else {
				if (n_src->nchn == 1) callback_process_loop<1>(vc, i_chan, n_src, si, outbuflen, sfx_vol);
				else                  callback_process_loop<2>(vc, i_chan, n_src, si, outbuflen, sfx_vol);
				return false;
			}
		};
		upd_silence();
		
		for (; si < outbuflen/2; ++si)
		{
			// frame proc
			
			if (vc.frame_left)
			{
				cur.add(mod);
				--vc.frame_left;
				if (!vc.frame_left)
				{
					cur = vc.next;
					if (cur.rev == &rev_zero)
						cur.rev = nullptr;
				}
			}
			else if (vc.flags & Channel::STOP) {
				if (vc.flags & Channel::IS_OBJ) {
					if (!vc.subsrc && src->is_loop)
						vc.subsrc = rnd_stat().range_index(vc.p_snd->data.size() - 1, 1);
				}
				else free_channel(i_chan);
				si = outbuflen; // outer break
				break;
			}
				
			// sample proc
#define CALC(POS) int s_i = POS; float s_t = POS - s_i; s_i *= NC
			float smp[2];
			{
				CALC(vc.pb_pos);
				for (int i=0; i<NC; ++i)
					smp[i] = lerp(float(src->d[s_i + i]), float(src->d[s_i + i + NC]), s_t);
			}
			if (cur.t_reverb >= reverb_t_thr)
			{
				if (cur.rev == vc.next.rev) {
					for (int j=0; j<reverb_lines; ++j) {
						float t = (*cur.rev)[j].first;
						float g = (*cur.rev)[j].second;
						CALC(vc.pb_pos - t + src->reverb_offset);
						s_i %= src->len;
						for (int i=0; i<NC; ++i)
							smp[i] += cur.t_reverb * g *
									  lerp(float(src->d[s_i + i]), float(src->d[s_i + i + NC]), s_t);
					}
				}
				else {
					float ft = 1 - float(vc.frame_left) / vc.frame_length;
					for (int j=0; j<reverb_lines; ++j) {
						float t = lerp((*cur.rev)[j].first,  (*vc.next.rev)[j].first,  ft);
						float g = lerp((*cur.rev)[j].second, (*vc.next.rev)[j].second, ft);
						CALC(vc.pb_pos - t + src->reverb_offset);
						s_i %= src->len;
						for (int i=0; i<NC; ++i)
							smp[i] += cur.t_reverb * g *
									  lerp(float(src->d[s_i + i]), float(src->d[s_i + i + NC]), s_t);
					}
				}
			}
#undef CALC
			if (NC == 1) {
				float out1 = smp[0] * (1.f - cur.lowpass);
				float out2 = (vc.lpass[0] + lowpass_alpha * (smp[0] - vc.lpass[0])) * cur.lowpass;
				vc.lpass[1] = vc.lpass[0] = out1 + out2;
				float out = (out1 + out2 * lowpass_amp) * sfx_vol;
				for (int i=0; i<2; ++i)
					mix_buffer[si*2+i] += cur.kpan[i] * out;
			}
			else {
				for (int i=0; i<2; ++i) {
					float out1 = smp[i] * (1.f - cur.lowpass);
					float out2 = (vc.lpass[i] + lowpass_alpha * (smp[i] - vc.lpass[i])) * cur.lowpass;
					vc.lpass[i] = out1 + out2;
					mix_buffer[si*2+i] += cur.kpan[i] * (out1 + out2 * lowpass_amp) * sfx_vol;
				}
			}
			
			// fin
			
			vc.pb_pos += cur.t_pitch * vc.dopp_mul;
			if (vc.pb_pos >= src->len)
			{
				if (src->is_loop && ((vc.flags & Channel::IS_OBJ) || vc.subsrc == 0))
				{
					if (vc.subsrc != 0) {
						vc.silence_left = vc.loop_period - src->len;
						upd_silence();
					}
					int old = vc.subsrc;
					vc.subsrc = rnd_stat().range_index(vc.p_snd->data.size() - 1, 1);
					if (vc.subsrc == old) vc.pb_pos -= src->len * (int(vc.pb_pos + 0.5) / src->len);
					else if (!newsub()) return;
					continue;
				}
				if (vc.subsrc != static_cast<int>(vc.p_snd->data.size()) - 1) {
					vc.subsrc = static_cast<int>(vc.p_snd->data.size()) - 1;
					if (!newsub()) return;
					continue;
				}
				if (vc.flags & Channel::IS_OBJ) {
					vc.frame_left = 0;
					vc.flags |= Channel::STOP;
					vc.flags &= ~Channel::IS_ACTIVE;
					vc.upd_at = DONT_UPDATE;
					vc.subsrc = vc.pb_pos = 0;
					break;
				}
				free_channel(i_chan);
				break;
			}
		}
	}
	void callback_process(const int outbuflen, const float sfx_vol)
	{
		for (int i_chan = 0; i_chan < static_cast<int>(chans.size()); ++i_chan)
		{
			auto& vc = chans[i_chan];
			if (vc && vc.is_proc()) {
				SoundData* src = &vc.p_snd->data[vc.subsrc];
				int si=0;
				if (src->nchn == 1) callback_process_loop<1>(vc, i_chan, src, si, outbuflen, sfx_vol);
				else                callback_process_loop<2>(vc, i_chan, src, si, outbuflen, sfx_vol);
			}
		}
	}
	void callback_convert(float *outbuf, int outbuflen)
	{
		float lim_max = 0;
		for (int i=0; i<outbuflen; ++i) {
			lim_max = std::max(lim_max, std::abs(mix_buffer[i]));
		}
		lim_max /= std::numeric_limits<int16_t>::max();
		if (lim_max > 1) {
			limit_mul = std::min(limit_mul, 1.f / (lim_max + 0.1f));
			limit_pause = limiter_pause.seconds() * sample_rate;
		}
		if (limit_pause > 0) {
			auto passed = float(outbuflen/2) / sample_rate;
			limit_mul += limiter_pause_incr * (passed / limiter_pause.seconds());
			limit_pause -= outbuflen/2;
		}
		else {
			auto passed = float(outbuflen/2) / sample_rate;
			limit_mul += (0.999 - limit_mul) * (passed / limiter_restore.seconds());
		}
		
		float gen_vol = g_vol * limit_mul;
		for (int i=0; i<outbuflen; ++i) {
			float res = mix_buffer[i] / std::numeric_limits<int16_t>::max();
			outbuf[i] = res * gen_vol;
		}
	}
	void callback(float *outbuf, int outbuflen, bool /*underrun*/)
	{
#define STAGE(IX, ...)\
	[&]{auto t0 = TimeSpan::current(); __VA_ARGS__; auto dt = TimeSpan::current() - t0;\
	    dbg_sum[IX].first += dt; dbg_sum[IX].second = std::max(dbg_sum[IX].second, dt);\
		if (!IX) dbg_times[i_dbg_times] = dt;}()
		STAGE(0,
		
		const bool ui_only = g_ui_only;
		const vec2fp lstr_pos = g_lstr_pos;
		const samplen_t update_length = upd_period_all.seconds() * sample_rate;
		
		STAGE(1, callback_music(outbuflen));
		
		{	std::unique_lock lock(chan_lock);
			update_accum += outbuflen/2;
			if (update_accum >= update_length)
			{
				if (!ui_only) STAGE(2, callback_visibility(lstr_pos));
				STAGE(3, callback_update(ui_only, lstr_pos, update_length));
				update_step += update_accum / update_length;
				dbg_count_upd++;
			}
			STAGE(4, callback_process(outbuflen, g_sfx_vol));
		}
		STAGE(5, callback_convert(outbuf, outbuflen));
		
		); // STAGE(0,
		dbg_count++;
		i_dbg_times = (i_dbg_times + 1) % dbg_times.size();
	}
};



#include <SDL2/SDL.h>
#if USE_PORTAUDIO
#include <portaudio.h>
#endif

struct Dev_Profile : AudioOutputDevice {
	std::atomic<int> run = 1;
	std::thread thr;
	Dev_Profile(SoundEngine_Impl* u, int n_samples): thr([this, u, n = 2*n_samples]
	{
		set_this_thread_name("sound profile");
		std::vector<float> buf;
		buf.resize(n);
		while (run) {
			if (run == 2) u->callback(buf.data(), n, false);
			precise_sleep(u->callback_len);
		}
	}) {}
	void set_pause(bool on) {
		run = on ? 1 : 2;
	}
	~Dev_Profile() {
		run = false;
		thr.join();
	}
};
AudioOutputDevice* audiodev_profile(SoundEngine_Impl* u, int n_samples)
{
	VLOGW("Using profiling stub instead of real audio!");
	return new Dev_Profile(u, n_samples);
}
struct Dev_SDL : AudioOutputDevice {
	SDL_AudioDeviceID dev = 0;
	Dev_SDL(SDL_AudioDeviceID dev): dev(dev) {}
	void set_pause(bool on) {
		SDL_PauseAudioDevice(dev, on);
	}
	~Dev_SDL() {
		SDL_CloseAudioDevice(dev);
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
	}
};
AudioOutputDevice* audiodev_sdl2(SoundEngine_Impl* u, const char *api_name, const char *dev_name, int sample_rate, int n_samples)
{
	VLOGI("Using SDL audio");
	
	if (!api_name) {
		if (SDL_InitSubSystem(SDL_INIT_AUDIO))
			THROW_FMTSTR("SDL_InitSubSystem failed - {}", SDL_GetError());
	}
	else {
		if (SDL_AudioInit(api_name))
			THROW_FMTSTR("SDL_AudioInit failed - {}", SDL_GetError());
	}
	
	SDL_AudioSpec spec = {};
	spec.freq = sample_rate;
	spec.channels = 2;
	spec.format = AUDIO_F32SYS;
	spec.samples = n_samples;
	spec.callback = [](void *ud, Uint8 *s, int sn){
		static_cast<SoundEngine_Impl*>(ud)->callback(pointer_cast<float*>(s), sn / sizeof(float), false);};
	spec.userdata = u;
	
	SDL_AudioDeviceID dev = SDL_OpenAudioDevice(dev_name, 0, &spec, nullptr, 0);
	if (!dev) {
		const char *err = SDL_GetError();
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		THROW_FMTSTR("SDL_OpenAudioDevice failed - {}", err);
	}
	return new Dev_SDL(dev);
}
#if USE_PORTAUDIO
struct Dev_PA : AudioOutputDevice {
	PaStream* dev = nullptr;
	Dev_PA(PaStream* dev): dev(dev) {}
	void set_pause(bool on) {
		if (auto err = on? Pa_StopStream(dev) : Pa_StartStream(dev))
			THROW_FMTSTR("Pa_StartStream() failed - {}", Pa_GetErrorText(err));
	}
	~Dev_PA() {
		if (auto err = Pa_CloseStream(dev)) VLOGE("Pa_CloseStream() failed - {}", Pa_GetErrorText(err));
		if (auto err = Pa_Terminate()) VLOGE("Pa_Terminate() failed - {}", Pa_GetErrorText(err));
	}
};
AudioOutputDevice* audiodev_portaudio(SoundEngine_Impl* u, const char *api_name, const char *dev_name, int sample_rate, int n_samples)
{
	VLOGI("Using PortAudio: {}", Pa_GetVersionText());
	
	if (auto err = Pa_Initialize()) THROW_FMTSTR("Pa_Initialize() failed - {}", Pa_GetErrorText(err));
	if (api_name || dev_name)
		THROW_FMTSTR("API and device selection not implemented");
	
	PaStreamParameters spec = {};
	spec.device = Pa_GetDefaultOutputDevice();
	spec.channelCount = 2;
	spec.sampleFormat = paFloat32;
	spec.suggestedLatency = 0;
	
	PaStream* dev;
	if (auto err = Pa_OpenStream(&dev, nullptr, &spec, sample_rate, n_samples, paClipOff,
		[](const void*, void* s, unsigned long sn,
		   const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags fs, void* u)
		{	static_cast<SoundEngine_Impl*>(u)->callback(pointer_cast<float*>(s), 2*sn, !!(fs & paOutputUnderflow));
			return static_cast<int>(paContinue);}
	    , u))
	{
		if (auto err = Pa_Terminate()) VLOGE("Pa_Terminate() failed - {}", Pa_GetErrorText(err));
		THROW_FMTSTR("Pa_OpenStream() failed - {}", Pa_GetErrorText(err));
	}
	return new Dev_PA(dev);
}
#else
AudioOutputDevice* audiodev_portaudio(SoundEngine_Impl*, const char *, const char *, int, int) {
	THROW_FMTSTR("Built without PortAudio support");
}
#endif



static SoundEngine* rni;
bool SoundEngine::init(bool profile_mode) {
	try {
		rni = new SoundEngine_Impl(profile_mode);
		return true;
	}
	catch (std::exception& e) {
		VLOGE("SoundEngine::init() failed - {}", e.what());
		return false;
	}
}
SoundEngine* SoundEngine::get() {return rni;}
SoundEngine::~SoundEngine() {rni = nullptr;}
