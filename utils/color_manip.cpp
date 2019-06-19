#include "vaslib/vas_log.hpp"
#include "color_manip.hpp"

FColor::FColor( uint32_t rgba, float mul )
{
	r = mul * ( rgba >> 24)        / 255.f;
	g = mul * ((rgba >> 16) & 255) / 255.f;
	b = mul * ((rgba >> 8)  & 255) / 255.f;
	a =       ( rgba        & 255) / 255.f;
}
FColor::FColor( float r, float g, float b, float a ) : r(r), g(g), b(b), a(a)
{}
float& FColor::operator[] (int i) {
	if		(i == 0) return r;
	else if (i == 1) return g;
	else if (i == 2) return b;
	ASSERT( i == 3, "FColor::[] invalid index" );
	return a;
}
const float& FColor::operator[] (int i) const {
	if		(i == 0) return r;
	else if (i == 1) return g;
	else if (i == 2) return b;
	ASSERT( i == 3, "FColor::[] invalid index" );
	return a;
}
FColor& FColor::operator+= (float f) {
	for (size_t i=0; i<3; ++i) (*this)[i] += f;
	return *this;
}
FColor& FColor::operator*= (float f) {
	for (size_t i=0; i<3; ++i) (*this)[i] *= f;
	return *this;
}
FColor& FColor::operator+= (const FColor& c) {
	for (size_t i=0; i<4; ++i) (*this)[i] += c[i];
	return *this;
}
FColor& FColor::operator-= (const FColor& c) {
	for (size_t i=0; i<4; ++i) (*this)[i] -= c[i];
	return *this;
}
uint32_t FColor::to_px() const
{
	uint32_t c = 0;
	c |= std::max( 0, std::min( 255, int(r * 255) ) ); c <<= 8;
	c |= std::max( 0, std::min( 255, int(g * 255) ) ); c <<= 8;
	c |= std::max( 0, std::min( 255, int(b * 255) ) ); c <<= 8;
	c |= std::max( 0, std::min( 255, int(a * 255) ) );
	return c;
}
FColor FColor::rgb_to_hsv() const
{
	// adapted from https://stackoverflow.com/a/6930407
	
	float min = std::min(r, std::min(g, b));
	float max = std::max(r, std::max(g, b));
	float dt = max - min;
	
	float h;
	
	if (dt < 1e-2) h = 0;
	else
	{
		if		(r >= max) h = (g - b) / dt;
		else if (g >= max) h = (b - r) / dt + 2;
		else               h = (r - g) / dt + 4;
		
		h /= 6;
		if (h < 0) h += 1;
	}
	
	return { h, max > 1e-2? dt / max : 0, max, a };
}
FColor FColor::hsv_to_rgb() const
{
	float h = r * 360;
	
	float vm = (1. - g) * b;
	float ak = (b - vm) * fmod(h, 60) / 60;
	float vi = vm + ak;
	float vd = b - ak;
	
	if		(h < 60)  return { b,  vi, vm, a };
	else if (h < 120) return { vd, b,  vm, a };
	else if (h < 180) return { vm, b,  vi, a };
	else if (h < 240) return { vm, vd, b,  a };
	else if (h < 300) return { vi, vm, b,  a };
	else              return { b,  vm, vd, a };
}
