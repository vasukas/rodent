#include <cstring>
#include <SDL2/SDL_rect.h>
#include "vas_math.hpp"



vec2fp cossin_ft(float x)
{
//	return {cos(x), sin(x)};
	
	const int table_size = 1024;
	static_assert(table_size % 4 == 0);
	
	static float table [table_size + 1];
	static bool is_first = true;
	
	if (is_first)
	{
		is_first = false;
		for (int i = 0; i < table_size; ++i) table[i] = sinf( i * M_PI*2 / table_size );
		table[table_size] = table[0];
	}
	
	if (!std::isfinite(x)) return {1, 0};
	x = wrap_angle_2(x);
	x /= M_PI*2;
	
	x *= table_size;
	int i = static_cast<int>(x);
	x -= i;
	
	if (i < 0) i += table_size;
	i %= table_size;
	int j = (i + table_size /4) % table_size;
	
	return {
		lerp( table[j], table[j+1], x ), // cosine
		lerp( table[i], table[i+1], x ), // sine
	};
}



// from Wikipedia
uint isqrt(uint n)
{
	int sh = 2;
	unsigned int nshd = n >> sh;
	
	while (nshd && nshd != n)
	{
		sh += 2;
		nshd = n >> sh;
	}
	sh -= 2;
	
	uint r = 0;
	while (sh != -2)
	{
		r <<= 1;
		uint cr = r+1;
		if (cr*cr <= (n >> sh)) r = cr;
		sh -= 2;
	}
	return r;
}

float fast_invsqrt(float x)
{
	if constexpr (std::numeric_limits<float>::is_iec559 && sizeof(float) == sizeof(uint32_t))
	{
		float x2 = x/2;
		uint32_t i;
		std::memcpy(&i, &x, 4);
		i = 0x5f3759df - (i >> 1);
		std::memcpy(&x, &i, 4);
		x *= 1.5f - (x2 * x * x);
//		x *= 1.5f - (x2 * x * x);
		return x;
	}
	else return 1.f / std::sqrt(x);
}

float wrap_angle_2(float x)
{
	int i = static_cast<int>( x / (M_PI*2.) );
	x -= i * (M_PI*2.);
	if (x < 0) x += M_PI*2.;
	return x;
}
float wrap_angle(float x)
{
	int i = static_cast<int>(x / M_PI);
	x -= i * M_PI;
	return x;
}



vec2i vec2i::get_rotated (double angle) const
{
	float c = cos(angle), s = sin(angle);
	return vec2i (
		c * x - s * y,
		s * x + c * y
	);
}
void vec2i::rotate (double angle)
{
	*this = get_rotated (angle);
}
void vec2i::fastrotate (float angle)
{
	vec2fp cs = cossin_ft(angle);
	int t = x;
	x = cs.x * t - cs.y * y;
	y = cs.y * t + cs.x * y;
}
vec2fp vec2i::get_norm() const
{
	float t = len();
	return { x / t, y / t };
}
vec2i::operator vec2fp() const
{
	return vec2fp (x, y);
}
bool is_in_polygon(vec2i p, const vec2i* ps, size_t pn)
{
	int side = 0;
	for (size_t i=0; i<pn; ++i)
	{
		int dot = ps[i].x * p.x + ps[i].y * p.y;
		if (dot < 0) dot = -1; else if (dot > 0) dot = 1;
		
		if (!side) side = dot;
		else if (side != dot) return false;
	}
	return true;
}



float vec2fp::angle() const {
	float a = std::atan2(y, x);
	return std::isfinite(a)? a : 0.f;
}
vec2fp vec2fp::get_rotated (double angle) const
{
	float c = cos(angle), s = sin(angle);
	return vec2fp (
		c * x - s * y,
		s * x + c * y
	);
}
void vec2fp::rotate (double angle)
{
	*this = get_rotated (angle);
}
void vec2fp::fastrotate (float angle)
{
	vec2fp cs = cossin_ft(angle);
	float t = x;
	x = cs.x * t - cs.y * y;
	y = cs.y * t + cs.x * y;
}
vec2fp vec2fp::get_norm() const
{
	float t = len();
	return {x / t, y / t};
}
void vec2fp::norm()
{
	float t = len();
	x /= t; y /= t;
}
vec2fp vec2fp::get_rotate (float cos, float sin)
{
	return {
		cos * x - sin * y ,
		sin * x + cos * y };
}
void vec2fp::rotate (float cos, float sin)
{
	float t = x;
	x = cos * t - sin * y;
	y = sin * t + cos * y;
}



std::optional<vec2fp> lineseg_intersect(vec2fp a1, vec2fp a2, vec2fp b1, vec2fp b2, float eps)
{
	auto t = line_intersect_t(a1, a2 - a1, b1, b2 - b1, eps);
	if (t &&
	    t->first  > -eps && t->first  < 1+eps &&
	    t->second > -eps && t->second < 1+eps
	   )
		return lerp(a1, a2, t->first);
	return {};
}
std::optional<std::pair<float, float>> line_intersect_t(vec2fp a, vec2fp at, vec2fp b, vec2fp bt, float eps)
{
	float im = cross(at, bt);
	if (std::fabs(im) < eps) return {};
	
	b -= a;
	float t = cross(b, bt) / im;
	float u = cross(b, at) / im;
	return std::make_pair(t, u);
}



bool Rect::intersects(const Rect& r) const
{
	vec2i a1 = upper(), b1 = r.upper();
	return off.x < b1.x && a1.x > r.off.x && off.y < b1.y && a1.y > r.off.y;
}
bool Rect::contains(vec2i p) const
{
	vec2i a1 = upper();
	return off.x <= p.x && a1.x >= p.x && off.y <= p.y && a1.y >= p.y;
}
Rect::operator Rectfp() const
{
	return Rectfp( lower(), upper(), false );
}
Rect::operator SDL_Rect() const
{
	return {off.x, off.y, sz.x, sz.y};
}
void Rect::set(const SDL_Rect& r)
{
	off = {r.x, r.y};
	sz  = {r.w, r.h};
}
Rect calc_intersection(const Rect& A, const Rect& B)
{
	// taken from SDL2
	
	if (A.sz == vec2i(0,0) || B.sz == vec2i(0,0)) return {{}, {}, true};
	
	int Amin, Amax, Bmin, Bmax;
	Rect res;

    Amin = A.lower().x;
    Amax = Amin + A.sz.x;
    Bmin = B.lower().x;
    Bmax = Bmin + B.sz.x;
    if (Bmin > Amin)
        Amin = Bmin;
    res.off.x = Amin;
    if (Bmax < Amax)
        Amax = Bmax;
    res.sz.x = Amax - Amin;

    Amin = A.lower().y;
    Amax = Amin + A.sz.y;
    Bmin = B.lower().y;
    Bmax = Bmin + B.sz.y;
    if (Bmin > Amin)
        Amin = Bmin;
    res.off.y = Amin;
    if (Bmax < Amax)
        Amax = Bmax;
    res.sz.y = Amax - Amin;
	
	return res;
}
Rect get_bound(const Rect& a, const Rect& b)
{
	return {min(a.lower(), b.lower()), max(a.upper(), b.upper()), false};
}



std::array <vec2fp, 4> Rectfp::rotate( float cs, float sn ) const
{	
	auto hsz = size() / 2;
	
	float xd = cs * hsz.x - sn * hsz.y;
	float yd = sn * hsz.x + cs * hsz.y;
	
	float zd = cs * hsz.x + sn * hsz.y;
	float wd = sn * hsz.x - cs * hsz.y;

	return
	{{
		{a.x - xd, a.y - yd},
		{a.x + zd, a.y + wd},
        {a.x - zd, a.y - wd},
        {a.x + xd, a.y + yd}
	}};
}
std::array <vec2fp, 4> Rectfp::rotate_fast( float rad ) const
{
	vec2fp cs = cossin_ft(rad);
	return rotate( cs.x, cs.y );
}
void Rectfp::merge( const Rectfp& r )
{
	a = min( a, r.a );
	b = max( b, r.b );
}
bool Rectfp::overlaps( const Rectfp& r ) const
{
	// ...
	return a.x < r.b.x && b.x > r.a.x && a.y < r.b.y && b.y > r.a.y;
}
bool Rectfp::contains( const Rectfp& r ) const
{
	return a.x <= r.a.x && a.y <= r.a.y && 
	       b.x >= r.b.x && b.y >= r.b.y;
}



vec2fp Transform::apply(vec2fp p) const {return (p + pos).get_rotated(rot);}
vec2fp Transform::reverse(vec2fp p) const {return p.get_rotated(-rot) - pos;}
void Transform::combine(const Transform& t)
{
	pos += t.pos.get_rotated(rot);
	rot += t.rot;
}
void Transform::combine_reversed(const Transform& t)
{
	rot -= t.rot;
	pos -= t.pos.get_rotated(rot);
}
Transform Transform::get_combined(const Transform& t) const {Transform r = *this; r.combine(t); return r;}
void Transform::add(const Transform& t)
{
	pos += t.pos;
	rot += t.rot;
}
Transform Transform::get_add(const Transform& t) const {Transform r = *this; r.add(t); return r;}
Transform Transform::operator * (float t) const {return Transform{pos * t, rot * t};}
void Transform::operator *= (float t) {pos *= t, rot *= t;}
