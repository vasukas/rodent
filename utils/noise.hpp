// Various random and noise generation functions

#ifndef NOISE_HPP
#define NOISE_HPP

#include <cstddef>
#include <random>



struct RandomGen
{
	std::mt19937 gen;
	std::uniform_real_distribution<> d_real;
	std::normal_distribution<> d_norm;
	
	RandomGen();
	bool flag();
	double range_n(); ///< uniform distribution [0; 1]
	double range_n2(); ///< uniform distribution [-1; 1]
	double range(double v0, double v1);
	size_t range_index(size_t num, size_t off = 0); ///< [off, num)
	
	double normal(); ///< normal distribution (half time in [-1; 1])
	double normal_fixed(); ///< (almost) normal distribution (strictly [-1; 1])
	
	std::string save() const;
	bool load(const std::string& s);
	
	template <typename T>
	T& random_el(std::vector<T> &els) {return els[range_index(els.size())];}
	
	template <typename T>
	void shuffle(std::vector<T>& els) {
		if (els.empty()) return;
		for (size_t i = els.size() - 1; i > 0; --i)
			std::swap(els[i], els[range_index(i)]);
	}
};

/// Returns static rndgen
RandomGen& rnd_stat();

#endif // NOISE_HPP
