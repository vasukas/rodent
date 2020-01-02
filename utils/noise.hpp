// Various random and noise generation functions

#ifndef NOISE_HPP
#define NOISE_HPP

#include <array>
#include <random>



struct RandomGen
{
	RandomGen();
	bool flag();
	double range_n(); ///< uniform distribution [0; 1]
	double range_n2(); ///< uniform distribution [-1; 1]
	double range(double v0, double v1);
	int int_range(int v0, int v1);
	size_t range_index(size_t num, size_t off = 0); ///< [off, num)
	
	double normal(); ///< normal distribution (half time in [-1; 1])
	double normal_fixed(); ///< (almost) normal distribution (strictly [-1; 1])
	
	std::string save() const;
	bool load(const std::string& s);
	void set_seed(uint32_t s);
	
	template <typename T>
	T& random_el(std::vector<T> &els) {return els[range_index(els.size())];}
	
	template <typename T>
	void shuffle(std::vector<T>& els) {
		if (els.empty()) return;
		for (size_t i = els.size() - 1; i > 0; --i)
			std::swap(els[i], els[range_index(i)]);
	}
	
	template <typename T, size_t N>
	const T& random_el(const std::array<std::pair<T, float>, N>& vs) {
		float t = range_n();
		for (auto& v : vs) if (t < v.second) return v.first;
		return vs.back().first;
	}
	
private:
	std::mt19937 gen;
	std::uniform_real_distribution<> d_real;
	std::normal_distribution<> d_norm;
};

/// Returns static rndgen
RandomGen& rnd_stat();



/// Normalize set of (value, chance) pairs
template <typename T, size_t N>
constexpr std::array<std::pair<T, float>, N>
normalize_chances(std::array<std::pair<T, float>, N> in)
{
	std::array<std::pair<T, float>, N> out;
	float k = 0;
	for (size_t i=0; i<N; ++i) k += in[i].second;
	k = 1 / k;
	
	float t = 0;
	for (size_t i=0; i<N; ++i) {
		t += k * in[i].second;
		out[i].first = in[i].first;
		out[i].second = t;
	}
	return out;
}

#endif // NOISE_HPP
