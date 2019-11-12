#include <sstream>
#include "vaslib/vas_log.hpp"
#include "noise.hpp"



RandomGen::RandomGen(): d_real(0,1), d_norm(0, 1) {}
bool RandomGen::flag()
{
	return range_n() < 0.5;
}
double RandomGen::range_n()
{
	return d_real(gen);
}
double RandomGen::range_n2()
{
	return d_real(gen) * 2 - 1;
}
double RandomGen::range(double v0, double v1)
{
	return v0 + range_n() * (v1 - v0);
}
size_t RandomGen::range_index(size_t num, size_t off)
{
	ASSERT(num > off, "RandomGen::range_index() on empty or negative range");
	return round (range (off, num - 1));
}
double RandomGen::normal()
{
	return d_norm(gen);
}
double RandomGen::normal_fixed()
{
	return std::max(-1., std::min(1., normal() / 3));
}
std::string RandomGen::save() const
{
	std::stringstream ss;
	ss << gen;
	return ss.str();
}
bool RandomGen::load(const std::string& s)
{
	std::stringstream ss(s);
	ss >> gen;
	return !ss.fail();
}



RandomGen& rnd_stat()
{
	static RandomGen g;
	return g;
}
