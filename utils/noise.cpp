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
int RandomGen::int_range(int v0, int v1)
{
	return round( range(v0 - 0.5, v1 + 0.5) );
}
size_t RandomGen::range_index(size_t num, size_t off)
{
	ASSERT(num >= off, "RandomGen::range_index() on empty or negative range");
	return int_range(off, num - 1);
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
void RandomGen::load(const std::string& s)
{
	std::stringstream ss(s);
	ss >> gen;
	if (ss.fail())
		throw std::runtime_error("RandomGen::load() failed");
}
void RandomGen::set_seed(uint32_t s)
{
	gen.seed(s);
}



RandomGen& rnd_stat()
{
	thread_local RandomGen g;
	return g;
}
