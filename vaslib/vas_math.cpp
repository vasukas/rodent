#include <cstring>
#include <SDL2/SDL_rect.h>
#include "vaslib/vas_math.hpp"



struct sin_lut_t
{
	static const int table_size = 1024;
	static_assert(table_size % 4 == 0);
	
	float *table;
	
	sin_lut_t() {
		table = new float [table_size + 1];
		for (int i = 0; i < table_size; ++i) table[i] = sinf( i * M_PI*2 / table_size );
		table[table_size] = table[0];
	}
	~sin_lut_t() {
		delete[] table;
	}
};
static sin_lut_t sin_lut;

float sine_lut_norm(float x)
{
	if (!std::isfinite(x)) return 0;
	if (x < 0) x = 1 - x;
	
	x *= sin_lut.table_size;
	int i = static_cast<int>(x);
	x -= i;
	
	i %= sin_lut.table_size;
	return lerp( sin_lut.table[i], sin_lut.table[i+1], x );
}
vec2fp cossin_lut(float x)
{
//	return {cos(x), sin(x)};
	
	if (!std::isfinite(x)) return {1, 0};
	x = wrap_angle_2(x);
	x /= M_PI*2;
	
	x *= sin_lut.table_size;
	int i = static_cast<int>(x);
	x -= i;
	
	if (i < 0) i += sin_lut.table_size;
	i %= sin_lut.table_size;
	int j = (i + sin_lut.table_size /4) % sin_lut.table_size;
	
	return {
		lerp( sin_lut.table[j], sin_lut.table[j+1], x ), // cosine
		lerp( sin_lut.table[i], sin_lut.table[i+1], x ), // sine
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
	float y = std::fmod(x, M_PI*2);
	return y < 0 ? y + M_PI*2 : y;
}
float wrap_angle(float x)
{
	if (x > 0) return std::fmod(x + M_PI, M_PI*2) - M_PI;
	else       return std::fmod(x - M_PI, M_PI*2) + M_PI;
}
float angle_delta(float target, float current)
{
	float delta = target - current;
//	while (delta < -M_PI) delta += M_PI*2;
//	while (delta >  M_PI) delta -= M_PI*2;
	
	int i = static_cast<int>(delta / M_PI);
	if		(i < 0) i = (-i + 1)/2;
	else if (i > 0) i = -(i + 1)/2;
	
	delta += i * M_PI * 2;
	return delta;
}



vec2fp vec2i::rotate (double angle) const
{
	double c = cos(angle), s = sin(angle);
	return vec2fp(
		c * x - s * y,
		s * x + c * y
	);
}
vec2fp vec2i::fastrotate (float angle) const
{
	vec2fp cs = cossin_lut(angle);
	return vec2fp(
		cs.x * x - cs.y * y,
		cs.y * x + cs.x * y
	);
}
vec2i vec2i::minmax() const
{
	return std::abs(x) < std::abs(y)
		? vec2i{std::abs(x), std::abs(y)}
		: vec2i{std::abs(y), std::abs(x)};
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



float vec2fp::fastangle() const {
	return angle(); // placeholder
}
float vec2fp::angle() const {
	float a = std::atan2(y, x);
	return std::isfinite(a)? a : 0.f;
}
vec2fp& vec2fp::rotate (double angle)
{
	double c = cos(angle), s = sin(angle);
	float t = x;
	x = c * t - s * y;
	y = s * t + c * y;
	return *this;
}
vec2fp& vec2fp::fastrotate (float angle)
{
	vec2fp cs = cossin_lut(angle);
	float t = x;
	x = cs.x * t - cs.y * y;
	y = cs.y * t + cs.x * y;
	return *this;
}
vec2fp vec2fp::minmax() const
{
	return std::abs(x) < std::abs(y)
		? vec2fp{std::abs(x), std::abs(y)}
		: vec2fp{std::abs(y), std::abs(x)};
}
vec2fp& vec2fp::norm()
{
	float t = len();
	x /= t; y /= t;
	return *this;
}
vec2fp& vec2fp::norm_to(float n)
{
	float t = n / len();
	x *= t; y *= t;
	return *this;
}
vec2fp& vec2fp::limit_to(float n)
{
	float cur = len_squ();
	if (cur > n*n) {
		float t = n / std::sqrt(cur);
		x *= t; y *= t;
	}
	return *this;
}
vec2fp& vec2fp::rotate (float cos, float sin)
{
	float t = x;
	x = cos * t - sin * y;
	y = sin * t + cos * y;
	return *this;
}



vec2fp slerp (const vec2fp &v0, const vec2fp &v1, float t)
{
	float dp = dot(v0, v1);
	if (dp > 0.9995) return lerp(v0, v1, t).norm();
	
	vec2fp v2 = (v1 - dp * v0).norm();
	auto [c, s] = cossin_lut(t * std::acos(dp));
	return v0 * c + v2 * s;
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
float lineseg_perpen_t(vec2fp a, vec2fp b, vec2fp c)
{
//	ASSERT(!a.equals(b, 1e-5));
	vec2fp rb = b - a;
	vec2fp rc = c - a;
	return (rc.x * rb.x + rc.y * rb.y) / (rb.x * rb.x + rb.y * rb.y);
}
std::optional<vec2fp> lineseg_perpen(vec2fp a, vec2fp b, vec2fp p)
{
	float t = lineseg_perpen_t(a, b, p);
	if (t < 0 || t > 1) return {};
	return lerp(a, b, t);
}
std::pair<float, vec2fp> fit_rect(vec2fp size, vec2fp into)
{
	vec2fp pk = into / size;
	float k = std::min(pk.x, pk.y);
	return {k, (into - size * k) / 2};
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
bool Rect::contains_le(vec2i p) const
{
	vec2i a1 = upper();
	return off.x <= p.x && a1.x > p.x && off.y <= p.y && a1.y > p.y;
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
void Rect::map(callable_ref<void(vec2i p)> f) const
{
	for (int y = lower().y; y < upper().y; ++y)
	for (int x = lower().x; x < upper().x; ++x)
		f({x, y});
}
bool Rect::map_check(callable_ref<bool(vec2i p)> f) const
{
	for (int y = lower().y; y < upper().y; ++y)
	for (int x = lower().x; x < upper().x; ++x)
		if (!f({x, y})) return false;
	return true;
}
void Rect::map_outer(callable_ref<void(vec2i p)> f) const
{
	for (int y = lower().y; y < upper().y; ++y) {
		f({ lower().x - 1, y });
		f({ upper().x,     y });
	}
	for (int x = lower().x; x < upper().x; ++x) {
		f({ x, lower().y - 1 });
		f({ x, upper().y,    });
	}
	f({ lower().x - 1, lower().y - 1 });
	f({ upper().x,     lower().y - 1 });
	f({ lower().x - 1, upper().y });
	f({ upper().x,     upper().y });
}
void Rect::map_inner(callable_ref<void(vec2i p)> f) const
{
	for (int y = lower().y + 1; y < upper().y - 1; ++y) {
		f({ lower().x,     y });
		f({ upper().x - 1, y });
	}
	for (int x = lower().x + 1; x < upper().x - 1; ++x) {
		f({ x, lower().y    });
		f({ x, upper().y - 1, });
	}
	f({ lower().x,     lower().y     });
	f({ upper().x - 1, lower().y     });
	f({ lower().x,     upper().y - 1 });
	f({ upper().x - 1, upper().y - 1 });
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
uint min_distance(const Rect& a, const Rect& b)
{
	auto au = a.upper();
	auto bu = b.upper();
	int xd, yd;
	
	if		(a.off.x >= bu.x) xd = a.off.x - bu.x;
	else if (b.off.x >= au.x) xd = b.off.x - au.x;
	else xd = 0;
	
	if		(a.off.y >= bu.y) yd = a.off.y - bu.y;
	else if (b.off.y >= au.y) yd = b.off.y - au.y;
	else yd = 0;
	
	return std::min(xd, yd);
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
	vec2fp cs = cossin_lut(rad);
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
bool Rectfp::contains(vec2fp p) const
{
	return a.x < p.x && b.x > p.x && a.y < p.y && b.y > p.y;
}
bool Rectfp::contains(vec2fp p, float width) const
{
	return a.x < p.x - width && b.x > p.x + width &&
	       a.y < p.y - width && b.y > p.y + width;
}



vec2fp Transform::apply(vec2fp p) const {return (p + pos).rotate(rot);}
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
void Transform::operator *= (float t) {pos *= t; rot *= t;}
