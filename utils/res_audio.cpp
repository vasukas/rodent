#if USE_OPENMPT
#define VAS_LOG_OSTREAM 1
#endif
#include <SDL2/SDL_audio.h>
#include "vaslib/vas_file.hpp"
#include "vaslib/vas_log.hpp"
#include "res_audio.hpp"



class AudioFile {
public:
	virtual ~AudioFile() = default;
	virtual int read(int16_t *buffer, int pos, int size) = 0;
	virtual int get_len() = 0;
	virtual int get_rate() = 0;
	virtual int get_chn() = 0; ///< Number of channels
	virtual bool has_subsongs() {return false;}
	virtual void select_subsong(int index) {(void) index;}
};



struct WAV_Header {
	static constexpr int size = 44;
	
	int channels;
	int sample_rate;
	int bytes;
	int datalen;
	
	const char *read(File& f) {
		char s[4];
		f.read(s, 4); // chunk id
		if (std::memcmp(s, "RIFF", 4))
			return "not RIFF container";
		
		f.r32N(); // chunk size
		f.read(s, 4); // format
		if (std::memcmp(s, "WAVE", 4))
			return "format is not 'WAVE'";
		
		f.read(s, 4); // subchunk id
		if (std::memcmp(s, "fmt ", 4))
			return "first subchunk is not 'fmt '";
		
		f.r32N(); // subchunk size
		int fmt = f.r16L(); // format
		if (fmt != 1)
			return "format is not uncompressed PCM";
		
		channels = f.r16L();
		sample_rate = f.r32L();
		f.r32N(); // byte rate
		f.r16N(); // block align
		
		int bps = f.r16L();
		if		(bps == 8)  bytes = 1;
		else if (bps == 16) bytes = 2;
		else return "unsupported format - only uint8 and int16 are supported";
		
		f.read(s, 4); // subchunk id
		if (std::memcmp(s, "data", 4))
			return "second subchunk is not 'data'";
		datalen = f.r32N();
		
		return {};
	}
};

class AudioFile_WAV : public AudioFile {
public:
	std::unique_ptr<File> file;
	WAV_Header hd;
	int length;
	
	AudioFile_WAV(const char *filename) {
		file = File::open_ptr(filename, File::OpenExisting | File::OpenRead);
		if (auto error = hd.read(*file))
			THROW_FMTSTR("WAV: invalid header - {}", error);
		length = std::min<int>(hd.datalen / hd.bytes, (file->get_size() - WAV_Header::size) / hd.bytes);
	}
	int read(int16_t *buffer, int pos, int size) {
		file->seek(pos * hd.bytes + WAV_Header::size);
		file->read(buffer, size * hd.bytes);
		if (hd.bytes == 1) {
			auto src = static_cast<uint8_t*>(static_cast<void*>(buffer));
			for (int i = size-1; i != -1; --i)
				buffer[i] = src[i];
		}
		return size; // read throws on error
	}
	int get_len() {
		return length;
	}
	int get_rate() {
		return hd.sample_rate;
	}
	int get_chn() {
		return hd.channels;
	}
};



#if USE_OPUSFILE
#include <opusfile.h>

static const char *opus_error(int err) {
	static std::string s;
	s = std::to_string(err);
	return s.c_str();
}

class AudioFile_Opus : public AudioFile {
public:
	OggOpusFile* file = nullptr;
	int n_chn;
	
	AudioFile_Opus(const char *filename) {
		int err;
		file = op_open_file(filename, &err);
		if (!file)
			THROW_FMTSTR("op_open_file failed - {}", opus_error(err));
		
		if (op_link_count(file) > 1)
			VLOGW("op_link_count > 1, only first is used (\"{}\")", filename);
		
		n_chn = op_channel_count(file, 0);
	}
	~AudioFile_Opus() {
		op_free(file);
	}
	int read(int16_t *buffer, int pos, int size) {
		op_pcm_seek(file, pos / get_chn());
		int ret = op_read(file, buffer, size, nullptr);
		if (ret < 0)
			THROW_FMTSTR("op_read failed - {}", opus_error(ret));
		return ret * get_chn();
	}
	int get_len() {
		return op_pcm_total(file, 0) * get_chn();
	}
	int get_rate() {
		return 48000;
	}
	int get_chn() {
		return n_chn;
	}
};

#endif



#if USE_OPENMPT
#include <libopenmpt/libopenmpt.hpp>

class AudioFile_OpenMPT : public AudioFile {
public:
	std::string data;
	std::unique_ptr<openmpt::module> mod;
	vaslog_ostream mod_clog;
	int sample_rate;
	int pos_check = 0;
	
	AudioFile_OpenMPT(const char *filename, int sample_rate)
	    : mod_clog(LogLevel::Debug), sample_rate(sample_rate)
	{
		data = readfile(filename).value_or(std::string{});
		if (data.empty()) THROW_FMTSTR("AudioFile_Openmpt:: failed to read file");
		try {
			mod.reset(new openmpt::module(data.data(), data.size(), mod_clog));
		}
		catch (std::exception& e) {
			THROW_FMTSTR("AudioFile_Openmpt:: {}", e.what());
		}
		mod->set_repeat_count(-1);
	}
	int read(int16_t *buffer, int pos, int size) {
		if (pos_check != pos) THROW_FMTSTR("AudioFile_Openmpt::read() non-streaming read");
		int n = mod->read_interleaved_stereo(sample_rate, size /2, buffer) *2;
		pos_check = pos + n;
		return n;
	}
	int get_len() {
		return std::numeric_limits<int>::max();
	}
	int get_rate() {
		return sample_rate;
	}
	int get_chn() {
		return 2;
	}
	bool has_subsongs() {
		return true;
	}
	void select_subsong(int index) {
		mod->select_subsong(index);
	}
};

#endif



class AudioSource_File : public AudioSource {
public:
	std::unique_ptr<AudioFile> file;
	int p_length, p_channels;
	
	AudioSource_File(std::unique_ptr<AudioFile> p_file)
	    : file(std::move(p_file))
	{
		p_length = file->get_len();
		p_channels = file->get_chn();
	}
	int read(int16_t *buffer, int pos, int len) {
		return file->read(buffer, pos, len);
	}
	int length() {
		return p_length;
	}
	int num_channels() {
		return p_channels;
	}
	bool has_subsongs() {
		return file->has_subsongs();
	}
	void select_subsong(int index) {
		file->select_subsong(index);
	}
};

class AudioSource_Resample : public AudioSource {
public:
	thread_local static inline std::vector<int16_t> mid;
	std::unique_ptr<AudioFile> src;
	SDL_AudioCVT cvt;
	int out_chans;
	int out_length;
	
	int src_pos = 0;
	int next_dst_pos = 0;
	
	AudioSource_Resample(std::unique_ptr<AudioFile> p_src, int dst_rate)
	    : src(std::move(p_src))
	{
		int dst_chn = (src->get_chn() == 1 ? 1 : 2);
		if (-1 == SDL_BuildAudioCVT(&cvt, AUDIO_S16SYS, src->get_chn(), src->get_rate(), AUDIO_S16SYS, dst_chn, dst_rate)) {
			THROW_FMTSTR("SDL_BuildAudioCVT ({}x{} -> {}x{}) failed - {}",
			             src->get_chn(), src->get_rate(), dst_chn, dst_rate, SDL_GetError());
		}
		out_chans = dst_chn;
		out_length = src->get_len() * cvt.len_ratio;
	}
	int read(int16_t *dst, int dst_pos, int dst_len)
	{
		int orig_dst_len = dst_len;
		
		if (!dst_pos) next_dst_pos = 0;
		else if (dst_pos != next_dst_pos)
			THROW_FMTSTR("AudioSource_Resample::read() non-continious read");

		// read directly to output buffer as much as possbile
		{
			int src_len = dst_len / cvt.len_mult;
			
			cvt.buf = static_cast<Uint8*>(static_cast<void*>(dst));
			cvt.len = 2 * src->read(dst, src_pos, src_len);
			SDL_ConvertAudio(&cvt);
			
			dst     += cvt.len_cvt /2;
			dst_pos += cvt.len_cvt /2;
			dst_len -= cvt.len_cvt /2;
			src_pos += src_len;
		}
		
		// read to intermediate buffer
		if (dst_len)
		{
			int src_len = dst_len / cvt.len_ratio;
			mid.resize(src_len * cvt.len_mult);
			
			cvt.buf = static_cast<Uint8*>(static_cast<void*>(mid.data()));
			cvt.len = 2 * src->read(mid.data(), src_pos, src_len);
			SDL_ConvertAudio(&cvt);
			
			std::memcpy(dst, mid.data(), cvt.len_cvt);
			dst_len -= cvt.len_cvt /2;
			src_pos += src_len;
		}
		
		next_dst_pos += (orig_dst_len - dst_len);
		return orig_dst_len - dst_len;
	}
	int length() {
		return out_length;
	}
	int num_channels() {
		return out_chans;
	}
	bool has_subsongs() {
		return src->has_subsongs();
	}
	void select_subsong(int index) {
		src->select_subsong(index);
	}
};



AudioSource* AudioSource::open_stream(const char *filename, int sample_rate)
{
	static bool first = true;
	if (first) {
		first = false;
#if USE_OPUSFILE
		VLOGI("audio: opusfile!");
#else
		VLOGI("audio: opusfile not used");
#endif
#if USE_OPENMPT
		VLOGI("audio: libopenmpt API - {}.{}.{}", OPENMPT_API_VERSION_MAJOR, OPENMPT_API_VERSION_MINOR, OPENMPT_API_VERSION_PATCH);
		uint32_t v = openmpt::get_library_version();
		VLOGI("audio: libopenmpt version - {}.{}.{}", v >> 24, (v >> 16) & 0xff, v & 0xffff);
#else
		VLOGI("audio: libopenmpt not used");
#endif
	}
	
	try {
		std::unique_ptr<AudioFile> file;
		if (fexist(filename))
		{
			std::string ext = get_file_ext(filename);
			if (ext == "wav") file = std::make_unique<AudioFile_WAV>(filename);
			else if (ext == "opus") {
#if USE_OPUSFILE
				file = std::make_unique<AudioFile_Opus>(filename);
#else
				THROW_FMTSTR("built without opus support");
#endif
			}
#if USE_OPENMPT
			else if (openmpt::is_extension_supported(ext)) {
				file = std::make_unique<AudioFile_OpenMPT>(filename, sample_rate);
			}
#endif
			else THROW_FMTSTR("unsupported extension");
		}
		else {
			std::string fn;
			auto test = [&](const char *ext){
				fn = std::string(filename) + ext;
				return fexist(fn.c_str());
			};
			if		(test(".wav"))  file = std::make_unique<AudioFile_WAV >(fn.c_str());
#if USE_OPUSFILE
			else if (test(".opus")) file = std::make_unique<AudioFile_Opus>(fn.c_str());
#endif
			else THROW_FMTSTR("no such file, can't guess extension");
		}
		
		std::unique_ptr<AudioSource> src;
		if (file->get_rate() != sample_rate || (file->get_chn() != 1 && file->get_chn() != 2))
			src = std::make_unique<AudioSource_Resample>(std::move(file), sample_rate);
		
		if (!src) src = std::make_unique<AudioSource_File>(std::move(file));
		return src.release();
	}
	catch (std::exception& e) {
		VLOGE("AudioSource::open_stream() failed (file: \"{}\") - {}", filename, e.what());
		return nullptr;
	}
}
AudioSource::Loaded AudioSource::load(const char *filename, int sample_rate)
{
	try {
		std::unique_ptr<AudioSource> src(open_stream(filename, sample_rate));
		if (!src) return {};
		
		Loaded ret;
		ret.channels = src->num_channels();
		ret.data.resize(src->length());
		
		size_t p = 0;
		while (true) {
			int n = src->read(ret.data.data() + p, p, ret.data.size() - p);
			if (!n) break;
			p += n;
		}
		ret.data.resize(p);
		return ret;
	}
	catch (std::exception& e) {
		VLOGE("AudioSource::load() failed (file: \"{}\") - {}", filename, e.what());
		return {};
	}
}
