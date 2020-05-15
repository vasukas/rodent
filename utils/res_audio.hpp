#ifndef RES_AUDIO_HPP
#define RES_AUDIO_HPP

#include <cinttypes>
#include <vector>

class AudioSource
{
public:
	struct Loaded {
		std::vector<int16_t> data; // empty if failed to load
		int channels = 1;
	};
	
	static AudioSource* open_stream(const char *filename, int sample_rate);
	static Loaded load(const char *filename, int sample_rate);
	virtual ~AudioSource() = default;
	
	virtual int read(int16_t *buffer, int pos, int len) = 0; ///< Returns number of samples read
	virtual int length() = 0; ///< May not be exact
	virtual int num_channels() = 0; ///< Only 1 or 2 are allowed
};

#endif // RES_AUDIO_HPP
