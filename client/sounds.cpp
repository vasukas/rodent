#include <atomic>
#include <filesystem>
#include <future>
#include <mutex>
#include <unordered_set>
#include <box2d/b2_dynamic_tree.h>
#include <box2d/b2_chain_shape.h>
#include <box2d/b2_edge_shape.h>
#include <SDL2/SDL.h>
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

const float speed_of_sound = 340;
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

const float wall_dist_incr = 5;   // additional increase in distance per wall meter
const float wall_pass_incr = 0.5; // lowpass increase per wall meter

const float reverb_max_dist = 8; // max dist
const float reverb_dist_delta = 1; // params delta
const float reverb_line_maxvol = db_to_lin(-10); // max volume of single line
const float reverb_vol_max = 0.6; // max effect volume
const float reverb_raycast_delta = GameConst::cell_size; // how far apart rays can be
const int reverb_lines = 4; // 0 disables effect
const float reverb_t_thr = 1e-3; // effect is disabled if 't_reverb' is lower
const float reverb_min_percentage = 0.6; // of rays hitting walls when effect should be max

const float pan_dist_full = 15; // full panning active from that distance
const float pan_max_k = 0.6; // max panning value

const float chan_vol_max = db_to_lin(-6); // mix headroom
const float rndvol_0 = db_to_lin(-2);
const float rndvol_1 = 1;

const TimeSpan sound_stopfade = TimeSpan::ms(100);
const TimeSpan sound_fadein = TimeSpan::ms(25);
const TimeSpan music_crossfade = TimeSpan::seconds(5);
const TimeSpan music_pause_fade = TimeSpan::seconds(3);

const TimeSpan limiter_pause = TimeSpan::seconds(2);
const float limiter_pause_incr = 0.4 / limiter_pause.seconds();
const TimeSpan limiter_restore = TimeSpan::seconds(5);

using samplen_t = int; ///< Length in samples



struct SoundInfo;
struct SoundData
{
	std::vector<int16_t> d;
	samplen_t len = 0;
	bool is_mono = true;
	bool is_loop = true;
};
struct SoundInfo
{
	std::vector<SoundData> data; // begin, {loopvar}, end. Begin and end may be the same
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
					d.is_mono = (ld.channels == 1);
					d.len = d.d.size() / ld.channels;
				}
				if (d.d.empty()) {
					d.len = 1; // easy way to prevent division by zero
					d.d.push_back(0);
				}
				if (dbg_fns) dbg_fns->emplace(std::string(name));
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
static const std::vector<const char *> name_array = {
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
	enum {
		SUB_PEACE,
		SUB_AMBIENT,
		SUB_LIGHT,
		SUB_HEAVY,
		SUB_EPIC,
		SUB__TOTAL_COUNT
	};
	struct Track {
		// 'normal' file
		std::array<std::string, SUB__TOTAL_COUNT> fns;
		// tracker file
		std::string song_fn;
		std::array<int, SUB__TOTAL_COUNT> subs;
	};
	
	static constexpr TimeSpan tmo_ambient_to_peace = TimeSpan::seconds(120);
	static constexpr TimeSpan tmo_battle_to_ambient = TimeSpan::seconds(6);
	static constexpr TimeSpan tmo_longbattle = TimeSpan::seconds(120);
	static constexpr TimeSpan tmo_escalation = TimeSpan::seconds(120);
	static constexpr TimeSpan track_switch_max = TimeSpan::seconds(8*60); // 100% to switch
	static constexpr TimeSpan track_switch_min = TimeSpan::seconds(3*60); // 0% to switch
	
	SoundEngine::MusControl state = SoundEngine::MUSC_NO_AUTO;
	std::vector<Track> tracks;
	
	int track = 0;
	int cur_music = -1; // subtrack
	TimeSpan track_start; // how long playing
	
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
		while (!tkr.ended())
		{
			auto str = tkr.str();
			if (str == "track") {
				tracks.emplace_back();
				continue;
			}
			if (tracks.empty()) THROW_FMTSTR("Track not specified");
			
			if (str == "song") {
				tracks.back().song_fn = tkr.str();
				for (auto& i : tracks.back().subs) i = -1;
			}
			else
			{
				int i;
				if		(str == "peace")   i = SUB_PEACE;
				else if (str == "ambient") i = SUB_AMBIENT;
				else if (str == "light")   i = SUB_LIGHT;
				else if (str == "heavy")   i = SUB_HEAVY;
				else if (str == "epic")    i = SUB_EPIC;
				else THROW_FMTSTR("Unknown subtrack ID: {}", str);
				
				if (tracks.back().song_fn.empty()) {
					tracks.back().fns[i] = tkr.str();
				}
				else {
					tracks.back().subs[i] = tkr.i32();
				}
			}
		}
	}
	catch (std::exception& e) {
		auto p = tkr.calc_position();
		THROW_FMTSTR("SndEngMusic::load() failed to read config - {} [at {}:{}]", e.what(), p.first, p.second);
	}
	
	for (auto& tr : tracks)
	{
		if (tr.song_fn.empty()) {
			for (int i=0; i<SUB__TOTAL_COUNT; ++i) {
				if (tr.fns[i].empty()) {
					if (!i) THROW_FMTSTR("SndEngMusic::load() - 'peace' subtrack must be specified");
					tr.fns[i] = tr.fns[i-1];
				}
			}
		}
		else {
			for (int i=0; i<SUB__TOTAL_COUNT; ++i) {
				if (tr.subs[i] == -1) {
					if (!i) THROW_FMTSTR("SndEngMusic::load() - 'peace' subtrack must be specified");
					tr.subs[i] = tr.subs[i-1];
				}
			}
		}
	}
}
void SndEngMusic::step(SoundEngine& snd, TimeSpan now)
{
	if (state == SoundEngine::MUSC_NO_AUTO || tracks.empty()) {
		cur_music = -1;
		return;
	}
	
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
	if (cur_music != sel) {
		if ((sel == SUB_PEACE || sel == SUB_AMBIENT) &&
		    rnd_stat().range_n() < (TimeSpan::current() - track_start - track_switch_min)
				/ (track_switch_max - track_switch_min)) {
			track = rnd_stat().range_index(tracks.size());
		}
		cur_music = sel;
		
		auto& tr = tracks[track];
		if (tr.song_fn.empty()) snd.music(tr.fns[sel].c_str(), -1, false);
		else snd.music(tr.song_fn.c_str(), tr.subs[sel], false);
	}
}
void SoundEngine::check_unused_sounds()
{
	std::unordered_set<std::string> fns;
	VLOGI("Checking sounds...");
	auto info = load_sounds(22050, &fns);
	size_t bytes = 0;
	for (int i=0; i<SND_TOTAL_COUNT_INTERNAL; ++i) {
		bool any = false;
		for (auto& d : info[i].data) {if (d.len > 2) {any = true; bytes += 2 * d.d.capacity();}}
		if (!any) VLOGW("Silence {}", get_name((SoundId)i));
	}
	for (auto& e : std::filesystem::directory_iterator(HARDPATH_SOUNDS_PREFIX)) {
		if (fns.end() == fns.find(e.path().filename().replace_extension().u8string()))
			VLOGW("Unused file '{}'", e.path().u8string());
	}
	VLOGI("Check finished");
	
	VLOGI("Checking music...");
	SndEngMusic mc;
	mc.load();
	fns.clear();
	for (auto& tr : mc.tracks) {
		if (tr.song_fn.empty()) {for (auto& fn : tr.fns) fns.emplace(fn);}
		else fns.emplace(tr.song_fn);
	}
	for (auto& name : fns) {
		std::string s = std::string(HARDPATH_MUSIC_PREFIX) + name;
		delete AudioSource::open_stream(s.c_str(), 22050);
	}
	for (auto& e : std::filesystem::directory_iterator(HARDPATH_MUSIC_PREFIX)) {
		if (fns.end() == fns.find(e.path().filename().replace_extension().u8string()))
			VLOGW("Unused file '{}'", e.path().u8string());
	}
	VLOGI("Check finished");
}



class SoundEngine_Impl : public SoundEngine
{
public:
	// general
	std::atomic<float> g_vol = 1;
	std::atomic<float> g_sfx_vol = 1;
	std::atomic<bool> g_ui_only = true;
	vec2fp g_lstr_pos = {};
	
	SDL_AudioDeviceID devid = 0;
	int sample_rate;
	
	// settings
	RAII_Guard sett_g, sett_reinit;
	static inline std::string init_prev_api;
	static inline std::string init_prev_device;
	
	// resources
	std::vector<SoundInfo> snd_res;
	
	using ReverbPars = std::array<std::pair<float, float>, reverb_lines>;
	std::vector<ReverbPars> rev_pars;
	ReverbPars rev_zero = {};
	
	// channels
	struct ChannelFrame
	{
		float kpan[2] = {0, 0}; // channel volume
		float lowpass = 0; // wet coeff
		float t_pitch = 1;
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
			IS_NEW = 16, // just added
			IS_STATIC = 32 // never moves
		};
		
		// params
		SoundInfo* p_snd = nullptr;
		std::optional<vec2fp> w_pos;
		EntityIndex target;
		float p_pitch;
		float volume; // already multiplied by p_snd
		samplen_t loop_period;
		
		// playback
		int subsrc = 0;
		float pb_pos = 0;
		samplen_t silence_left = 0;
		samplen_t frame_left = 0;
		samplen_t frame_length = 0; // last
		ChannelFrame cur, next, mod;
		
		// effects data
		float lpass[2] = {0,0}; // lowpass filter memory
		float dopp_mul = 1; // doppler pitch multiplier
		
		// control
		int32 body; // sensor (if used) or nullptr
		int upd_at; // update step
		uint8_t flags = IS_ACTIVE | IS_NEW;
		
		explicit operator bool() const {return p_snd;}
		bool is_proc() const {return (flags & IS_ACTIVE) || frame_left;}
	};
	
	// control
	std::mutex chan_lock;
	SparseArray<Channel> chans;
	SndEngMusic musc;
	
	b2DynamicTree snd_detect; // detect active channels
	b2DynamicTree wall_detect;
	std::vector<b2EdgeShape> wall_shapes;
	
	samplen_t update_accum = 0;
	int update_step = 0;
	
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
	
	std::unique_ptr<AudioSource> musold_src;
	int musold_pos;
	samplen_t musold_left = 0;
	
	float mus_vol_tar = 1, mus_vol_cur = 1; // ui lock fade
	
	// debug
	TimeSpan callback_len;
	TimeSpan dbg_total_s, dbg_upd_s, dbg_proc_s;
	TimeSpan dbg_total_m, dbg_upd_m, dbg_proc_m;
	size_t dbg_count = 0;
	
	std::vector<TimeSpan> dbg_times;
	size_t i_dbg_times = 0;
	RAII_Guard dbg_menu;
	
	// simple limiter
	float limit_mul = 0.999;
	samplen_t limit_pause = 0;
	
	
	
	static b2AABB mk_aabb(vec2fp ctr, float radius) {
		return {{ctr.x - radius, ctr.y - radius}, {ctr.x + radius, ctr.y + radius}};
	}
	
	SoundEngine_Impl()
	{
		// init API
		
		if (AppSettings::get().audio_api.empty()) {
			if (SDL_InitSubSystem(SDL_INIT_AUDIO))
				THROW_FMTSTR("SDL_InitSubSystem failed - {}", SDL_GetError());
		}
		else {
			if (SDL_AudioInit(AppSettings::get().audio_api.data()))
				THROW_FMTSTR("SDL_AudioInit failed - {}", SDL_GetError());
		}
		RAII_Guard api_g([]{
			SDL_QuitSubSystem(SDL_INIT_AUDIO);
		});
		
		// init device
		
		const char *dev_name = AppSettings::get().audio_device.c_str();
		if (!std::strlen(dev_name)) dev_name = nullptr;

		SDL_AudioSpec spec = {};
		spec.freq = AppSettings::get().audio_rate;
		spec.channels = 2;
		spec.format = AUDIO_F32SYS;
		spec.samples = AppSettings::get().audio_samples;
		spec.callback = [](void *ud, Uint8 *s, int sn){
			static_cast<SoundEngine_Impl*>(ud)->callback(static_cast<float*>(static_cast<void*>(s)), sn / sizeof(float));};
		spec.userdata = this;
		
		devid = SDL_OpenAudioDevice(dev_name, 0, &spec, nullptr, 0);
		if (!devid)
			THROW_FMTSTR("SDL_OpenAudioDevice failed - {}", SDL_GetError());
		
		api_g.cancel();
		
		sample_rate = spec.freq;
		callback_len = TimeSpan::seconds(double(spec.samples) / sample_rate);
		VLOGI("Audio: {} Hz, {} samples, {:.3f}ms frame",
		      sample_rate, spec.samples, callback_len.micro() / 1000.f);
		
		// setup
		
		sett_g = AppSettings::get_mut().add_cb([this]{
			set_master_vol(AppSettings::get().audio_volume);
			set_sfx_vol(AppSettings::get().sfx_volume);
			set_music_vol(AppSettings::get().music_volume);
		});
		sett_reinit = AppSettings::get_mut().add_cb([]{
			if (init_prev_api != AppSettings::get().audio_api ||
			    init_prev_device != AppSettings::get().audio_device)
			{
				delete SoundEngine::get();
				SoundEngine::init();
			}
		}, false);
		init_prev_api = AppSettings::get().audio_api;
		init_prev_device = AppSettings::get().audio_device;
		
		mix_buffer.resize(sample_rate * 2);
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
		});
		
		SDL_PauseAudioDevice(devid, 0);
	}
	~SoundEngine_Impl()
	{
		TimeSpan exit_wait;
		set_pause(true);
		while (true)
		{
			{	std::unique_lock l1(chan_lock);
				std::unique_lock l2(music_lock);
				int an = 0;
				for (auto& c : chans) if (c.flags & Channel::IS_ACTIVE) ++an;
				if (!an && !mus_src && !musold_src && mus_vol_cur < 0.01)
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
		
		SDL_CloseAudioDevice(devid);
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		
		VLOGI("Audio thread times (avg, max):");
		auto show = [&](auto pref, auto sum, auto max){
			auto perc = [&](auto t) {return static_cast<int>(100 * (t / callback_len));};
			sum *= 1.f / dbg_count;
			VLOGI("  {}:  {:7}mks  {:3}%   {:7}mks  {:3}%", pref, sum.micro(), perc(sum), max.micro(), perc(max));
		};
		show("total", dbg_total_s, dbg_total_m);
		show("upd  ", dbg_upd_s, dbg_upd_m);
		show("proc ", dbg_proc_s, dbg_proc_m);
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
			wall_detect.CreateProxy(aabb, reinterpret_cast<void*>(static_cast<intptr_t>(wall_shapes.size() - 1)));
		}
	}
	void geom_static_clear() override
	{
		std::unique_lock lock(chan_lock);
		for (size_t i=0; i<wall_shapes.size(); ++i) wall_detect.DestroyProxy(i);
		wall_shapes.clear();
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
	void music(const char *name, int subtrack_index, bool disable_musc) override
	{
		if (disable_musc) music_control(MUSC_NO_AUTO);
		if (mus_load.valid()) mus_load.get();
		
		if (!name) {
			std::unique_lock lock(music_lock);
			if (mus_src) {
				musold_src = std::move(mus_src);
				musold_pos = mus_pos;
				musold_left = music_crossfade.seconds() * sample_rate;
			}
			mus_src.reset();
			mus_pos = 0;
			return;
		}
		else if (mus_prev_name == name && subtrack_index != -1) {
			std::unique_lock lock(music_lock);
			if (mus_src) {
				mus_src->select_subsong(subtrack_index);
				return;
			}
		}
		
		std::string s = std::string(HARDPATH_MUSIC_PREFIX) + name;
		mus_prev_name = name;
		
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
			}
			mus_src.reset(src);
			mus_pos = 0;
			
			if (subtrack_index != -1)
				mus_src->select_subsong(subtrack_index);
		});
	}
	void music_control(MusControl state) override
	{
		std::unique_lock lock(chan_lock);
		musc.state = state;
	}
	SoundObj::Id play(SoundObj::Id i_obj, const SoundPlayParams& pp, bool continious) override
	{
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
	void set_pause(bool is_paused) override
	{
		std::unique_lock lock1(chan_lock);
		std::unique_lock lock2(music_lock);
		g_ui_only = is_paused;
		mus_vol_tar = is_paused ? 0 : 1;
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
		vc.upd_at = -1;
		
		if (vc.flags & Channel::IS_NEW) {
			if (!(vc.flags & Channel::IS_OBJ)) free_channel(i);
			return;
		}
		
		vc.frame_left = sound_stopfade.seconds() * sample_rate;
		vc.next = vc.cur;
		for (int i=0; i<2; ++i) vc.next.kpan[i] = 0;
		vc.mod.diff(vc.cur, vc.next, 1.f / vc.frame_left);
	}
	void callback(float *outbuf, int outbuflen)
	{
		TimeSpan time0 = TimeSpan::current();
		
		// music
		
		bool zero_mix = true;
		{
			std::unique_lock lock(music_lock);
			if ((mus_src || musold_src) && (mus_vol_cur > 0.001 || mus_vol_tar > 0.001))
			{
				zero_mix = false;
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
						if (!n) mus_pos = 0;
					}
				}
				else
				{
					for (int i=0; i<outbuflen; ++i)
						mix_buffer[i] = 0;
				}
				if (musold_left)
				{
					samplen_t mus_left_full = music_crossfade.seconds() * sample_rate;
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
					
					if (mus_vol_cur == mus_vol_tar && mus_vol_cur <= 0.001)
					{
						musold_left = 0;
						musold_src.reset();
					}
				}
			}
		}
		if (zero_mix) {
			for (auto& s : mix_buffer) s = 0;
		}
		
		// update
		
		TimeSpan time_pre_upd = TimeSpan::current();
		
		update_accum += outbuflen/2;
		
		const bool ui_only = g_ui_only;
		const vec2fp lstr_pos = g_lstr_pos;
		const samplen_t update_length = upd_period_all.seconds() * sample_rate;
		const samplen_t frame_length = upd_period_one.seconds() * sample_rate;
		const samplen_t frame_fadein = std::min<samplen_t>(sound_fadein.seconds() * sample_rate, frame_length);
		const int upd_increment = std::max<int>(1, upd_period_all / upd_period_one);
		
		// check range
		if (update_accum >= update_length && !ui_only)
		{
			std::unique_lock lock(chan_lock);
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
			snd_detect.Query(&cb, mk_aabb(g_lstr_pos, dist_cull_radius));
			
			for (auto it = chans.begin(); it != chans.end(); ++it)
			{
				if (bool(it->flags & Channel::IS_ACTIVE) != bool(it->flags & Channel::WAS_ACTIVE))
				{
					if (it->flags & Channel::IS_ACTIVE) it->upd_at = update_step;
					else stop_channel(it.index());
				}
			}
		}
		
		// update stats
		while (update_accum >= update_length)
		{
			std::unique_lock lock(chan_lock);
			
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
				if (it->upd_at != update_step) continue;
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
				float wall = 0; // reverse wall factor (0 no wall, 1 full wall)
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
						
						if (reverb_lines && wall > 0.999f && (!(vc.flags & Channel::IS_STATIC) || (vc.flags & Channel::IS_NEW)))
						{
							float frac = 1;
							int n_rays = (2*M_PI * reverb_max_dist) / reverb_raycast_delta;
							int n_hits = 0;
							for (int i=0; i<n_rays; ++i)
							{
								float rot = 2*M_PI * i / n_rays;
								vec2fp dir = vec2fp(reverb_max_dist, 0).fastrotate(rot);
								bool was_hit = false;
								raycast(*vc.w_pos, *vc.w_pos + dir, [&](float f){
									frac = std::min(frac, f);
									was_hit = true;
								});
								if (was_hit) ++n_hits;
							}
							frm.t_reverb = reverb_vol_max * (1 - frac * frac);
							frm.t_reverb *= std::min(1.f, n_hits / reverb_min_percentage / n_rays);
							if (frm.t_reverb < reverb_t_thr) frm.rev = nullptr;
							else {
								int i = std::min<int>(frac * rev_pars.size(), rev_pars.size()-1);
								frm.rev = &rev_pars[i];
							}
						}
					}
				}
				
				frm.lowpass = 1 - wall;
				frm.kpan[0] = frm.kpan[1] = chan_vol_max * (1 - kdist) * vc.volume;
				
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
				
				if (bool(vc.cur.rev) != bool(vc.mod.rev)) {
					if (!vc.cur.rev) vc.cur.rev = &rev_zero;
					else vc.mod.rev = &rev_zero;
				}
			}
			
			update_accum -= update_length;
			++update_step;
		}
		
		TimeSpan time_pre_proc = TimeSpan::current();
		float sfx_vol = g_sfx_vol;
		
		// process
		
		{	std::unique_lock lock(chan_lock);
			for (auto it = chans.begin(); it != chans.end(); ++it)
			{
				if (!it->is_proc()) continue;
				auto& vc = *it;
				
				ChannelFrame& cur = vc.cur;
				ChannelFrame& mod = vc.mod;
				
				int si=0;
				while (si < outbuflen/2)
				{
					if (vc.silence_left > 0) {
						int n = std::min(vc.silence_left, outbuflen/2 - si);
						si += n;
						vc.silence_left -= n;
						continue;
					}
					
					auto& src = vc.p_snd->data[vc.subsrc];
					for (; si<outbuflen/2; ++si)
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
							if (!(vc.flags & Channel::IS_OBJ)) free_channel(it.index());
							si = outbuflen; // outer break
							break;
						}
						
						// sample proc
						
						float smp[2];
						if (cur.t_reverb < reverb_t_thr)
						{
							float s_t = std::fmod(vc.pb_pos, 1);
							int i0 = vc.pb_pos;
							int i1 = (i0 + 1) % src.len;
							
							if (src.is_mono) {
								smp[0] = lerp(float(src.d[i0]), float(src.d[i1]), s_t);
								smp[1] = smp[0];
							}
							else {
								for (int i=0; i<2; ++i)
									smp[i] = lerp(float(src.d[i0*2 + i]), float(src.d[i1*2 + i]), s_t);
							}
						}
						else
						{
							auto get = [&](float pos, int i){
								float s_t = std::fmod(pos, 1);
								int i0 = size_t(pos)    % src.len;
								int i1 = size_t(i0 + 1) % src.len;
								if (src.is_mono) return lerp(float(src.d[i0]), float(src.d[i1]), s_t);
								else return lerp(float(src.d[i0*2 + i]), float(src.d[i1*2 + i]), s_t);
							};
							float ft = 1 - float(vc.frame_left) / vc.frame_length;
							for (int i=0; i<2; ++i) {
								smp[i] = get(vc.pb_pos, i);
								for (int j=0; j<reverb_lines; ++j) {
									float t = lerp((*cur.rev)[i].first,  (*vc.next.rev)[i].first,  ft);
									float g = lerp((*cur.rev)[i].second, (*vc.next.rev)[i].second, ft);
									smp[i] += cur.t_reverb * g * get(vc.pb_pos - t, i);
								}
							}
						}
	
						for (int i=0; i<2; ++i) {
							float sf = (smp[i] + vc.lpass[i]) /2;
							mix_buffer[si*2+i] += cur.kpan[i] * lerp(smp[i], sf, cur.lowpass) * sfx_vol;
							vc.lpass[i] = sf;
						}
						
						// fin
						
						vc.pb_pos += cur.t_pitch * vc.dopp_mul;
						if (vc.pb_pos >= src.len)
						{
							if ((vc.flags & Channel::IS_OBJ) && src.is_loop)
							{
								if (vc.subsrc != 0) vc.silence_left = vc.loop_period - src.len;
								int old = vc.subsrc;
								vc.subsrc = rnd_stat().range_index(vc.p_snd->data.size() - 1, 1);
								if (vc.subsrc == old) vc.pb_pos -= src.len;
								else vc.pb_pos = 0;
								break; // restart loop with new sub
							}
							if (vc.subsrc != static_cast<int>(vc.p_snd->data.size()) - 1) {
								vc.subsrc = static_cast<int>(vc.p_snd->data.size()) - 1;
								vc.pb_pos = 0;
								break; // restart loop with new sub
							}
							if (vc.flags & Channel::IS_OBJ) {
								vc.frame_left = 0;
								vc.flags |= Channel::STOP;
								vc.flags &= ~Channel::IS_ACTIVE;
								vc.upd_at = -1;
								vc.subsrc = vc.pb_pos = 0;
								break; // will exit outer loop
							}
							free_channel(it.index());
							si = outbuflen; // outer break
							break;
						}
					}
				}
			}
		}
		
		// convert
		
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
		
		TimeSpan time1 = TimeSpan::current();
		auto set = [](auto& sum, auto& max, auto time) {
			max = std::max(max, time);
			sum += time;
		};
		set(dbg_total_s, dbg_total_m, time1 - time0);
		set(dbg_upd_s,   dbg_upd_m,   time_pre_proc - time_pre_upd);
		set(dbg_proc_s,  dbg_proc_m,  time1 - time_pre_proc);
		dbg_count++;
		
		dbg_times[i_dbg_times] = time1 - time0;
		i_dbg_times = (i_dbg_times + 1) % dbg_times.size();
	}
};



static SoundEngine* rni;
bool SoundEngine::init() {
	try {
		rni = new SoundEngine_Impl;
		return true;
	}
	catch (std::exception& e) {
		VLOGE("SoundEngine::init() failed - {}", e.what());
		return false;
	}
}
SoundEngine* SoundEngine::get() {return rni;}
SoundEngine::~SoundEngine() {rni = nullptr;}
