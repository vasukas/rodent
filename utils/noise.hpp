// Various random and noise generation functions

#ifndef NOISE_HPP
#define NOISE_HPP

#include <cstddef>

/// Returns random number in specified range
double rnd_range(double r0 = 0., double r1 = 1.);

/// Returns random number in specified range, excluding last
size_t rnd_uint(size_t min, size_t max);

/// Returns normalized random number in [-1; 1] range
double rnd_gauss();

/// Returns value in range [-1; 1]
//double perlin_noise(double x, double y, double z);

/// Returns value in range [-1; 1]
//double perlin_noise_oct(double x, double y, double z, double pers, double frqm, int octaves);

#endif // NOISE_HPP
