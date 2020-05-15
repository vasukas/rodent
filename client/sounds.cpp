#include <atomic>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <variant>
#include <SDL2/SDL.h>
#include <box2d/box2d.h>
#include "client/presenter.hpp"
#include "core/hard_paths.hpp"
#include "core/settings.hpp"
#include "core/vig.hpp"
#include "game/game_core.hpp"
#include "utils/noise.hpp"
#include "utils/res_audio.hpp"
#include "utils/tokenread.hpp"
#include "vaslib/vas_log.hpp"
#include "vaslib/vas_string_utils.hpp"
#include "sounds.hpp"

static float db_to_lin(float db) {
	return std::pow(10, db/10);
}
static float sound_rtt(float dist) {
	return dist * 2 / 340; // roundtrip time using speed of sound
}

const TimeSpan thread_sleep = TimeSpan::ms(20);
const TimeSpan t_frame_length = TimeSpan::ms(20); // must be same as thread_sleep or shorter
// const TimeSpan t_stopfade_length = TimeSpan::seconds(0.5); // DISABLED - see usage of ChannelFrame::t_frame()

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

const TimeSpan music_transition_time = TimeSpan::seconds(5);
const TimeSpan music_pause_fade = TimeSpan::seconds(3);



struct SoundInfo;
struct SoundData
{
	std::vector<int16_t> d;
	int len = 0;
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
				THROW_FMTSTR("unknown name: {}", tkr.raw());
			
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
			
			while (!tkr.raw().empty() && std::islower(tkr.raw()[0]))
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
				else THROW_FMTSTR("unknown option: {}", tkr.str());
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
	chan = b.chan;
	b.chan = SNDENG_CHAN_NONE;
}
SoundObj& SoundObj::operator=(SoundObj&& b) noexcept
{
	stop();
	chan = b.chan;
	b.chan = SNDENG_CHAN_NONE;
	return *this;
}
void SoundObj::update(const SoundPlayParams& pars)
{
	if (auto p = SoundEngine::get())
		chan = p->play(chan, pars, true);
}
void SoundObj::update(Entity& ent, SoundPlayParams pars)
{
	pars.pos = ent.get_pos();
	pars.target = ent.index;
	update(pars);
}
void SoundObj::stop()
{
	if (chan != SNDENG_CHAN_NONE) {
		SoundEngine::get()->stop(chan);
		chan = SNDENG_CHAN_NONE;
	}
}



struct SndEngMusic {
	SoundEngine::MusControl state = SoundEngine::MUSC_NO_AUTO;
	std::vector<std::vector<std::string>> track_fns;
	
	static constexpr TimeSpan tmo_ambient_to_peace = TimeSpan::seconds(120);
	static constexpr TimeSpan tmo_battle_to_ambient = TimeSpan::seconds(6);
	static constexpr TimeSpan tmo_longbattle = TimeSpan::seconds(120);
	static constexpr TimeSpan tmo_escalation = TimeSpan::seconds(120);
	static constexpr TimeSpan track_switch_max = TimeSpan::seconds(8*60); // 100% to switch
	static constexpr TimeSpan track_switch_min = TimeSpan::seconds(3*60); // 0% to switch
	
	enum {
		SUB_PEACE,
		SUB_AMBIENT,
		SUB_LIGHT,
		SUB_HEAVY,
		SUB_EPIC,
		SUB__TOTAL_COUNT
	};
	int track = 0;
	int cur_music = -1;
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
			if (tkr.is("track")) {
				track_fns.emplace_back().resize(SUB__TOTAL_COUNT);
				continue;
			}
			if (track_fns.empty()) THROW_FMTSTR("Track not specified");
			if		(tkr.is("peace"))   track_fns.back()[SUB_PEACE] = tkr.str();
			else if (tkr.is("ambient")) track_fns.back()[SUB_AMBIENT] = tkr.str();
			else if (tkr.is("light"))   track_fns.back()[SUB_LIGHT] = tkr.str();
			else if (tkr.is("heavy"))   track_fns.back()[SUB_HEAVY] = tkr.str();
			else if (tkr.is("epic"))    track_fns.back()[SUB_EPIC]  = tkr.str();
			else THROW_FMTSTR("Unknown subtrack ID: {}", tkr.raw());
		}
	}
	catch (std::exception& e) {
		auto p = tkr.calc_position();
		THROW_FMTSTR("SndEngMusic::load() failed to read config - {} [at {}:{}]", e.what(), p.first, p.second);
	}
	
	for (auto& tr : track_fns)
	{
		if (tr[SUB_PEACE]  .empty()) THROW_FMTSTR("SndEngMusic::load() - 'peace' subtrack must be specified");
		if (tr[SUB_AMBIENT].empty()) tr[SUB_AMBIENT] = tr[SUB_PEACE];
		if (tr[SUB_LIGHT]  .empty()) tr[SUB_LIGHT]   = tr[SUB_AMBIENT];
		if (tr[SUB_HEAVY]  .empty()) tr[SUB_HEAVY]   = tr[SUB_LIGHT];
		if (tr[SUB_EPIC]   .empty()) tr[SUB_EPIC]    = tr[SUB_HEAVY];
	}
}
void SndEngMusic::step(SoundEngine& snd, TimeSpan now)
{
	if (state == SoundEngine::MUSC_NO_AUTO || track_fns.empty()) {
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
			track = rnd_stat().range_index(track_fns.size());
		}
		cur_music = sel;
		snd.music(track_fns[track][sel].c_str(), false);
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
	VLOGI("Check finished - {}", string_display_bytesize(bytes));
	
	VLOGI("Checking music...");
	SndEngMusic mc;
	mc.load();
	fns.clear();
	for (auto& tr : mc.track_fns) for (auto& fn : tr) fns.emplace(fn);
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
	std::atomic<float> g_vol;
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
	
	// thread
	std::atomic<bool> thr_term = false;
	std::thread thr;
	
	// world
	std::mutex world_lock;
	b2World world = b2World({0,0});
	std::vector<float> cast_dists;
	
	SndEngMusic musc;
	
	// channels
	struct ChannelFrame
	{
		float kpan[2] = {0,0}; // channel volume
		float lowpass = 0; // wet coeff
		float t_reverb = 0;
		
		int t_left = 0; // samples
		float t_pitch = 1;
		ReverbPars* rev = nullptr;
		
		void diff(const ChannelFrame& prev, const ChannelFrame& next, float time_k) {
			t_left = next.t_left;
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
		float t_frame(int frame_len) {
			return float(t_left) / frame_len;
		}
	};
	struct ChannelStep
	{
		SoundInfo* info = nullptr;		
		float t_pitch = 1;
		bool is_static = true; // true if params never change
		bool loop = false;
		
		int init_delay = 0;
		int loop_period = 0;
		
		std::optional<vec2fp> w_pos;
		EntityIndex eid;
		
		bool stop = false; // shouldn't be processed
		int8_t new_st = 0; // not, just added, inited
		bool silent = false; // can't be heard now
		std::optional<ChannelFrame> next;
		float vol = 1;
		
		bool is_free() {
			return !info;
		}
		void free() {
			*this = ChannelStep{};
		}
	};
	struct StepInternal
	{
		int i; // channel index
		SoundInfo* info;
		std::optional<vec2fp> w_pos;
		
		ChannelFrame frm;
		bool stop = false; // shouldn't be processed
		bool silent = false; // can't be heard now
	};
	struct ChannelLive
	{
		SoundInfo* info = nullptr;
		int subsrc = 0; // subtrack
		
		float pb_pos = 0;
		float lpass[2] = {};
		ChannelFrame cur, mod, next;
		
		int delay_left = 0;
		int loop_period = 0;
		
		bool stop = false; // free as soon as frame is completed
		bool silent = false; // can't be heard now
		bool loop = false;
		
		bool is_free() {
			return !info;
		}
		void free() {
			*this = ChannelLive{};
		}
	};
	
	static constexpr int n_chns = 64; // hard limit
	
	std::mutex chan_lock; // for chns_step
	std::vector<ChannelStep> chns_step; // modified by step and live threads
	std::vector<ChannelLive> chns_live; // modified only by live thread
	std::vector<StepInternal> chns_iupd; // only for step function
	
	// audio
	std::vector<float> mix_buffer;
	std::vector<int16_t> load_buffer;
	
	int frame_len;
	float k_frame_time;
//	int stopfade_len;
	
	// music
	std::mutex music_lock;
	std::atomic<float> g_mus_vol = 0;
	std::thread mus_load;
	std::unique_ptr<AudioSource> mus_src;
	int mus_pos = 0;
	
	std::unique_ptr<AudioSource> musold_src;
	int musold_pos;
	int musold_left = 0, mus_left_full;
	
	float mus_vol_tar = 1, mus_vol_cur = 1; // ui lock fade
	
	// debug
	TimeSpan callback_len;
	TimeSpan dbg_audio_max;
	TimeSpan dbg_audio_sum;
	size_t dbg_audio_count = 0;
	
	RAII_Guard dbg_menu;
	
	
	
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
		
		chns_step.resize(n_chns);
		chns_live.resize(n_chns);
		chns_iupd.reserve(n_chns);
		
		mix_buffer.resize(spec.samples * 2);
		load_buffer.resize(spec.samples * 2);
		
		frame_len = t_frame_length.seconds() * sample_rate;
		k_frame_time = 1.f / frame_len;
//		stopfade_len = t_stopfade_length.seconds() * sample_rate;
		mus_left_full = music_transition_time.seconds() * sample_rate;
		
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
		
		thr = std::thread([this]{
			set_this_thread_name("soundstep");
			while (!thr_term) {
				TimeSpan t0 = TimeSpan::current();
				step();
				TimeSpan passed = TimeSpan::current() - t0;
				sleep(thread_sleep - passed);
			}
		});
		SDL_PauseAudioDevice(devid, 0);
		
		dbg_menu = vig_reg_menu(VigMenu::DebugRenderer, [this]
		{
			int an = 0, ln = 0;
			for (auto& c : chns_step) if (!c.is_free()) ++an;
			for (auto& c : chns_live) if (!c.is_free()) ++ln;
			vig_label_a("Sound channelss: {:2} / {:2} / {}\n", an, ln, n_chns);
		});
	}
	~SoundEngine_Impl()
	{
		SDL_PauseAudioDevice(devid, 1);
		
		thr_term = true;
		if (thr.joinable()) thr.join();
		if (mus_load.joinable()) mus_load.join();
		
		SDL_CloseAudioDevice(devid);
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		
		auto perc = [&](auto t) {return static_cast<int>(100 * (t / callback_len));};
		dbg_audio_sum.set_micro(dbg_audio_sum.micro() / dbg_audio_count);
		VLOGI("Audio thread times:\n  avg: {:7}mks  {:3}%\n  max: {:7}mks  {:3}%",
		      dbg_audio_sum.micro(), perc(dbg_audio_sum), dbg_audio_max.micro(), perc(dbg_audio_max));
	}
	void geom_static_add(Transform pos, const b2Shape& shp) override
	{
		std::unique_lock lock(world_lock);
		
		b2BodyDef bdef;
		bdef.position.Set(pos.pos.x, pos.pos.y);
		bdef.angle = pos.rot;
		b2Body* b = world.CreateBody(&bdef);
		
		b2FixtureDef fdef;
		fdef.shape = &shp;
		fdef.density = 1;
		b->CreateFixture(&fdef);
	}
	void geom_static_clear() override
	{
		std::unique_lock lock(world_lock);
		for (auto b = world.GetBodyList(); b;) {
			auto next = b->GetNext();
			if (!b->GetUserData()) world.DestroyBody(b);
			b = next;
		}
	}
	float get_master_vol() override
	{
		return g_vol;
	}
	void set_master_vol(float vol) override
	{
		g_vol = std::min(vol, 0.999f);
	}
	float get_music_vol() override
	{
		return g_mus_vol;
	}
	void set_music_vol(float vol) override
	{
		g_mus_vol = std::min(vol, 0.999f);
	}
	void music(const char *name, bool disable_musc) override
	{
		if (disable_musc) music_control(MUSC_NO_AUTO);
		if (mus_load.joinable())
			mus_load.join();
		
		if (!name) {
			std::unique_lock lock(music_lock);
			mus_src.reset();
			musold_src.reset();
			return;
		}
		
		std::string s = std::string(HARDPATH_MUSIC_PREFIX) + name;
		mus_load = std::thread([this, s = std::move(s)]
		{
			set_this_thread_name("music load");
			auto src = AudioSource::open_stream(s.data(), sample_rate);
			if (!src) VLOGE("SoundEngine::music() failed (\"{}\")", s);
			
			std::unique_lock lock(music_lock);
			if (mus_src) {
				musold_src = std::move(mus_src);
				musold_pos = mus_pos;
				musold_left = mus_left_full;
			}
			mus_src.reset(src);
			mus_pos = 0;
		});
	}
	void music_control(MusControl state) override
	{
		std::unique_lock lock(world_lock);
		musc.state = state;
	}
	int play(int i, const SoundPlayParams& pp, bool continious) override
	{
		std::unique_lock lock(chan_lock);
		auto& info = snd_res[pp.id];
		
		if (!info.ok()) {
			if (i >= 0) stop_internal(i);
			return SNDENG_CHAN_NONE;
		}
		if (pp.pos) {
			if (pp.pos->dist_squ(g_lstr_pos) > dist_cull_radius_squ)
				return SNDENG_CHAN_NONE;
		}
		
		auto& chns = chns_step;
		if (i >= 0 && chns[i].info != &info) {
			stop_internal(i);
			i = SNDENG_CHAN_NONE;
		}
		
		bool is_new = (i < 0);
		bool is_static = (i == SNDENG_CHAN_DONTCARE);
		if (is_new) {
			i=0;
			for (; i<n_chns; ++i) {
				if (chns[i].is_free())
					break;
			}
			if (i == n_chns)
				return SNDENG_CHAN_NONE;
		}
		
		auto& ch = chns[i];
		ch.info = &info;
		ch.w_pos = pp.pos;
		ch.eid = pp.target;
		if (is_new) ch.new_st = 1;
		ch.is_static = is_static && !ch.w_pos && !ch.eid;
		ch.loop = continious;
		ch.loop_period = pp.loop_period.seconds() * sample_rate;
		
		if (pp.t) {
			ch.t_pitch = lerp(info.spd_mut.first, info.spd_mut.second, *pp.t);
		}
		else {
			if (is_new && info.randomized)
				ch.t_pitch = lerp(info.spd_rnd.first, info.spd_rnd.second, rnd_stat().range_n());
			else
				ch.t_pitch = 1;
		}
		if (is_new) {
			ch.vol = info.random_vol ? rnd_stat().range(rndvol_0, rndvol_1) : 1;
			ch.vol *= clampf_n(pp.volume);
			ch.vol *= info.volume;
			if (ch.w_pos) {
				float d = ch.w_pos->dist(g_lstr_pos);
				ch.init_delay = sound_rtt(d) * sample_rate;
			}
		}
		return i;
	}
	void stop(int chan_id) override
	{
		std::unique_lock<std::mutex> lock;
		stop_internal(chan_id);
	}
	void stop_internal(int i, bool is_pause = false, bool is_force = false)
	{
		auto& chn = chns_step[i];
		auto& live = chns_live[i];
		
		if (live.is_free()) chn.free();
		else {
			chn.loop = false;
			if (is_force)
			{
				chn.loop = false;
				chn.next = live.cur;
				chn.next->t_left = frame_len;
				if (is_pause) chn.silent = true;
				else chn.stop = true;
			}
		}
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
			info.reserve(n_chns);
			{
				std::unique_lock l1(world_lock);
				std::unique_lock l2(chan_lock);
				
				for (int i=0; i<n_chns; ++i) {
					auto& chn = chns_live[i];
					if (chn.is_free()) continue;
					auto& stp = chns_step[i];
					auto& in = info.emplace_back();
					in.snd = chn.info;
					in.sub = chn.subsrc;
					in.pos = stp.w_pos;
				}
			}
			for (auto& in : info) {
				int st = in.sub == 0 ? 0 : (in.sub != (int) in.snd->data.size() - 1 ? 1 : 2);
				size_t i_snd = std::distance(snd_res.data(), in.snd);
				GamePresenter::get()->dbg_text(in.pos.value_or(lstr_pos), FMT_FORMAT("{}[{}]", name_array[i_snd], st));
			}
		}
		
		std::unique_lock l1(world_lock);
		std::unique_lock l2(chan_lock);
		g_lstr_pos = lstr_pos;
		
		for (int i=0; i<n_chns; ++i) {
			auto& chn = chns_step[i];
			if (!chn.is_free()) {
				if (auto ent = core.valid_ent(chn.eid))
					chn.w_pos = ent->get_pos();
			}
		}
		
		if (g_mus_vol > 0.01)
			musc.step(*this, core.get_step_time());
	}
	void set_pause(bool is_paused) override
	{
		std::unique_lock lock(chan_lock);
		g_ui_only = is_paused;
		mus_vol_tar = is_paused ? 0 : 1;
		if (!mus_src) mus_vol_cur = mus_vol_tar;
	}
	void step()
	{
		// gather state
		chns_iupd.clear();
		{
			std::unique_lock lock(chan_lock);
			bool ui_only = g_ui_only;
			
			for (int i=0; i<n_chns; ++i)
			{
				auto& chn = chns_step[i];
				if (!chn.is_free() && !chn.stop)
				{
					if (ui_only && !chn.info->is_ui) {
						stop_internal(i, true);
						continue;
					}
					if (chn.is_static && chn.new_st != 1)
						continue;
					
					auto& upd = chns_iupd.emplace_back();
					upd.i = i;
					upd.info = chn.info;
					upd.w_pos = chn.w_pos;
				}
			}
		}
		
		// calculate
		{
			std::unique_lock lock(world_lock);
			auto raycast = [&](vec2fp a, vec2fp b, callable_ref<void(float fraction)> f){
				struct CB : b2RayCastCallback {
					callable_ref<void(float)>* f;
					float ReportFixture(b2Fixture*, const b2Vec2&, const b2Vec2&, float fraction) override {
						(*f)(fraction);
						return 1;
					}
				};
				CB cb;
				cb.f = &f;
				world.RayCast(&cb, {a.x, a.y}, {b.x, b.y});
			};
			
			vec2fp lstr = g_lstr_pos;
			for (auto& upd : chns_iupd)
			{
				float kdist = 0; // relative distance (0 closest, 1 farthest)
				float wall = 0; // reverse wall factor (0 no wall, 1 full wall)
				float pan = 0, vpan = 0;
				
				const float min_dist = 1;
				if (upd.w_pos)
				{
					vec2fp dt = lstr - *upd.w_pos;
					float dist = dt.len_squ();
					
					if (dist > upd.info->max_dist_squ) {
						if (dist > dist_cull_radius_squ) {
							upd.stop = true;
						}
						upd.silent = true;
						continue;
					}
					else if (dist > min_dist)
					{
						dist = std::sqrt(dist);
						
						cast_dists.clear();
						raycast(lstr, *upd.w_pos, [&](float f){
							cast_dists.push_back(f);
						});
						
						std::sort(cast_dists.begin(), cast_dists.end());
						for (size_t i=0; i + 1 < cast_dists.size(); i += 2)
						{
							float w = (cast_dists[i+1] - cast_dists[i]) * dist;
							wall -= wall_pass_incr * w;
							
							dist += wall_dist_incr * w;
							if (dist > upd.info->max_dist)
								break;
						}
						if (dist > upd.info->max_dist) {
							upd.silent = true;
							continue;
						}
						
						kdist = dist / upd.info->max_dist; // linear decay
						wall = std::max(wall, 0.f);
						
						//
						
						float kxy = dot(dt / dist, vec2fp(1,0));
						pan = std::copysign(std::min(std::abs(kxy), pan_max_k), -kxy);
						vpan = std::copysign(1 - std::abs(kxy), -dt.y);
						
						float k = dist > pan_dist_full ? 1 : (dist - min_dist) / (pan_dist_full - min_dist);
						pan *= k;
						vpan *= k;
						
						//
						
						if (reverb_lines && wall > 0.999f)
						{
							float frac = 1;
							int n_rays = (2*M_PI * reverb_max_dist) / reverb_raycast_delta;
							int n_hits = 0;
							for (int i=0; i<n_rays; ++i)
							{
								float rot = 2*M_PI * i / n_rays;
								vec2fp dir = vec2fp(reverb_max_dist, 0).fastrotate(rot);
								bool was_hit = false;
								raycast(*upd.w_pos, *upd.w_pos + dir, [&](float f){
									frac = std::min(frac, f);
									was_hit = true;
								});
								if (was_hit) ++n_hits;
							}
							upd.frm.t_reverb = reverb_vol_max * (1 - frac * frac);
							upd.frm.t_reverb *= std::min(1.f, n_hits / reverb_min_percentage / n_rays);
							if (upd.frm.t_reverb < reverb_t_thr) upd.frm.rev = nullptr;
							else {
								int i = std::min<int>(frac * rev_pars.size(), rev_pars.size()-1);
								upd.frm.rev = &rev_pars[i];
							}
						}
					}
				}
				
				upd.frm.lowpass = 1 - wall;
				upd.frm.kpan[0] = upd.frm.kpan[1] =
					chan_vol_max * (1 - kdist);
				
				static const float sq2 = std::sqrt(2.f);
				auto cs = cossin_lut(M_PI_4 * (1 + pan));
				upd.frm.kpan[0] *= sq2 * std::abs(cs.x);
				upd.frm.kpan[1] *= sq2 * cs.y;
				upd.frm.lowpass = clampf_n(upd.frm.lowpass + vpan * lowpass_vertical_pan);
			}
		}
		
		// update state
		{
			std::unique_lock lock(chan_lock);
			for (auto& upd : chns_iupd)
			{
				auto& chn = chns_step[upd.i];
				if (chn.new_st == 1) chn.new_st = 2;
				
				if (upd.stop) {
					stop_internal(upd.i, false, true);
				}
				else if (upd.silent && !chn.silent) {
					stop_internal(upd.i, true);
				}
				else {
					chn.next = upd.frm;
					auto& f = *chn.next;
					f.t_left = frame_len;
					f.t_pitch = chn.t_pitch;
					for (auto& k : f.kpan) k *= chn.vol;
				}
			}
		}
	}
	void callback(float *outbuf, int outbuflen)
	{
		TimeSpan time0 = TimeSpan::current();
		
		// get updates
		{
			std::unique_lock lock(chan_lock);
			for (int i=0; i<n_chns; ++i)
			{
				auto& src = chns_step[i];
				auto& dst = chns_live[i];
				
				if (!src.is_free())
				{
					if (src.new_st) {
						if (src.new_st == 1) continue;
						src.new_st = 0;
						dst.info = src.info;
						dst.next = dst.cur = *src.next; // always set
						src.next.reset();
						dst.delay_left = src.init_delay;
					}
					if (dst.is_free()) {
						src.free();
					}
					else if (src.next) {
						dst.next = *src.next;
						src.next.reset();
						
						dst.mod.diff(dst.cur, dst.next, k_frame_time), 
						dst.stop = src.stop;
						dst.silent = src.silent;
					}
					if (bool(dst.cur.rev) != bool(dst.mod.rev)) {
						if (!dst.cur.rev) dst.cur.rev = &rev_zero;
						else dst.mod.rev = &rev_zero;
					}
					dst.loop = src.loop;
					dst.loop_period = src.loop_period;
				}
			}
		}
		
		// music
		
		bool zero_mix = true;
		{
			std::unique_lock lock(music_lock);
			bool fixed_vol = aequ(mus_vol_cur, mus_vol_tar, 1e-5f);
			auto zero_tar = [&]{return aequ(mus_vol_tar, 0, 1e-5f);};
			
			if (mus_src && !(fixed_vol && zero_tar()))
			{
				zero_mix = false;
				float vol = g_mus_vol;
				if (fixed_vol) vol *= mus_vol_cur;
				
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
				if (musold_left)
				{
					float t0 = float(musold_left) / mus_left_full;
					int len = std::min(musold_left, outbuflen /2);
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
				if (!fixed_vol)
				{
					float v0 = mus_vol_cur;
					float vchx = music_pause_fade.seconds() * (outbuflen/2) / sample_rate;
					if (mus_vol_cur < mus_vol_tar) mus_vol_cur = std::min(mus_vol_tar, mus_vol_cur + vchx);
					else                           mus_vol_cur = std::max(mus_vol_tar, mus_vol_cur - vchx);
					
					for (int i=0; i<outbuflen; ++i) {
						mix_buffer[i] *= lerp(v0, mus_vol_cur, float(i) / outbuflen);
					}
					
					if (mus_vol_cur == mus_vol_tar && zero_tar())
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
		
		// process
		
		for (int i=0; i<n_chns; ++i)
		{
			auto& chn = chns_live[i];
			if (chn.is_free())
				continue;
			
			ChannelFrame& cur = chn.cur;
			ChannelFrame& mod = chn.mod;
			
			int si=0;
			while (si<outbuflen/2)
			{
				if (chn.delay_left > 0) {
					int n = std::min(chn.delay_left, outbuflen/2 - si);
					si += n;
					chn.delay_left -= n;
					continue;
				}
				
				auto& src = chn.info->data[chn.subsrc];
				for (; si<outbuflen/2; ++si)
				{
					// frame proc
					
					if (mod.t_left) {
						cur.add(mod);
						--mod.t_left;
						if (!mod.t_left) {
							cur = chn.next;
							if (cur.rev == &rev_zero)
								cur.rev = nullptr;
						}
					}
					else if (chn.stop) {
						chn.free();
						si = outbuflen; // outer break
						break;
					}
					else if (chn.silent) {
						si = outbuflen; // outer break
						break;
					}
					
					// sample proc
					
					float smp[2];
					if (cur.t_reverb < reverb_t_thr)
					{
						float s_t = std::fmod(chn.pb_pos, 1);
						int i0 = chn.pb_pos;
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
						float ft = chn.mod.t_frame(frame_len);
						for (int i=0; i<2; ++i) {
							smp[i] = get(chn.pb_pos, i);
							for (int j=0; j<reverb_lines; ++j) {
								float t = lerp((*cur.rev)[i].first,  (*chn.next.rev)[i].first,  ft);
								float g = lerp((*cur.rev)[i].second, (*chn.next.rev)[i].second, ft);
								smp[i] += cur.t_reverb * g * get(chn.pb_pos - t, i);
							}
						}
					}

					for (int i=0; i<2; ++i) {
						float sf = (smp[i] + chn.lpass[i]) /2;
						mix_buffer[si*2+i] += cur.kpan[i] * lerp(smp[i], sf, cur.lowpass);
						chn.lpass[i] = sf;
					}
					
					// fin
					
					chn.pb_pos += cur.t_pitch;
					if (chn.pb_pos >= src.len)
					{
						if (chn.loop && src.is_loop)
						{
							if (chn.subsrc != 0) chn.delay_left = chn.loop_period - src.len;
							int old = chn.subsrc;
							chn.subsrc = rnd_stat().range_index(chn.info->data.size()-1, 1);
							if (chn.subsrc == old) chn.pb_pos -= src.len;
							else chn.pb_pos = 0;
							break; // restart loop with new sub
						}
						if (chn.subsrc != static_cast<int>(chn.info->data.size()) - 1) {
							chn.pb_pos = 0;
							chn.subsrc = static_cast<int>(chn.info->data.size()) - 1;
							break; // restart loop with new sub
						}
						
						chn.free();
						si = outbuflen; // outer break
						break;
					}
				}
			}
		}
		
		// convert
		
		float gen_vol = g_vol;
		for (int i=0; i<outbuflen; ++i) {
			float res = mix_buffer[i] / std::numeric_limits<int16_t>::max();
			outbuf[i] = res * 0.999 * gen_vol;
		}
		
		TimeSpan passed = TimeSpan::current() - time0;
		dbg_audio_max = std::max(dbg_audio_max, passed);
		dbg_audio_sum += passed;
		dbg_audio_count++;
	}
};



static SoundEngine* rni;
void SoundEngine::init() {
	try {rni = new SoundEngine_Impl;}
	catch (std::exception& e) {
		VLOGE("SoundEngine::init() failed - {}", e.what());
	}
}
SoundEngine* SoundEngine::get() {return rni;}
SoundEngine::~SoundEngine() {rni = nullptr;}
