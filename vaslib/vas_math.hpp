#ifndef VAS_MATH_HPP
#define VAS_MATH_HPP

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>
#include <optional>
#include "vas_cpp_utils.hpp"

using uint = unsigned int;

struct SDL_Rect;

struct Rect;
struct Rectfp;
struct vec2i;
struct vec2fp;



float sine_lut_norm(float x); ///< Table-lookup sine, x is [0, 1] representing [0, 2pi]
vec2fp cossin_lut(float rad); ///< Table-lookup cosine (x) + sine (y)

/// Integer square root
uint isqrt(uint value);

/// Quake III
float fast_invsqrt(float x);




/// Approximate comparison
template <typename T1, typename T2, typename T3>
bool aequ(T1 v, T2 c, T3 eps) {return std::fabs(v - c) < eps;}

inline float fracpart(float v) {return v - static_cast<int>(v);}

inline float clampf(float x, float min, float max) {return std::max(min, std::min(max, x));}
inline float clampf_n(float x) {return clampf(x, 0, 1);}
template <typename T> T clamp(T x, T min, T max) {return std::max(min, std::min(max, x));}

template <typename T1, typename T2, typename T3>
typename std::common_type_t<T1, T2>
lerp (T1 a, T2 b, T3 t) {return a * (1 - t) + b * t;}

template <typename T1>
T1 inv_lerp(T1 a, T1 b, T1 v) {return (v - a) / (b - a);}

template <typename T>
int int_round(T value) {return static_cast<int>(std::round(value));}

/// Absolute difference in ring of numbers modulo n; a,b ∈ [0, n)
template <typename T>
typename std::enable_if_t<std::is_signed_v<T>, T>
modulo_dist(T a, T b, T n) {
	T d1 = std::abs(a - b);
	return std::min(d1, n - d1);
}



constexpr float deg_to_rad(float x) {return x / 180.f * M_PI;}

float wrap_angle_2(float x); ///< Brings to [0; 2pi]
float wrap_angle(float x); ///< Brings to [-pi, +pi]

/// Returns delta for lerp from current to target (shortest path, [-pi; +pi])
float angle_delta(float target, float current);

/// Returns absolute difference between two unbound angles, [0; pi]
inline float abs_angle_diff(float a1, float a2) {return std::fabs(wrap_angle( a1 - a2 ));}

/// Linear interpolation between two angles, expressed in radians. Handles all cases
template <typename T1, typename T2, typename T3>
typename std::enable_if_t <
	std::is_floating_point_v<typename std::common_type_t<T1, T2, T3>>,
	typename std::common_type_t<T1, T2, T3> >
lerp_angle (T1 a, T2 b, T3 t) {return a + t * std::remainder(b - a, M_PI*2);}



/// 2D integer vector
struct vec2i {
	int x, y;
	
	vec2i() = default;
	vec2i(int x, int y): x(x),y(y) {}
	void set(int x, int y) {this->x = x; this->y = y;}
	vec2i& zero() {x = y = 0; return *this;}
	static vec2i one( int v ) { return {v, v};}
	
	void operator += (const vec2i& v) {x += v.x; y += v.y;}
	void operator -= (const vec2i& v) {x -= v.x; y -= v.y;}
	void operator *= (const vec2i& v) {x *= v.x; y *= v.y;}
	void operator /= (const vec2i& v) {x /= v.x; y /= v.y;}
	void operator *= (double f) {x = std::floor(x * f); y = std::floor(y * f);}
	void operator /= (double f) {x = std::floor(x / f); y = std::floor(y / f);}
	
	vec2i operator + (const vec2i& v) const {return {x + v.x, y + v.y};}
	vec2i operator - (const vec2i& v) const {return {x - v.x, y - v.y};}
	vec2i operator * (const vec2i& v) const {return {x * v.x, y * v.y};}
	vec2i operator / (const vec2i& v) const {return {x / v.x, y / v.y};}
	vec2i operator * (double f) const {return vec2i(std::floor(x * f), std::floor(y * f));}
	vec2i operator / (double f) const {return vec2i(std::floor(x / f), std::floor(y / f));}
	
	vec2i operator - () const {return vec2i(-x, -y);}
	
	bool operator == (const vec2i& v) const {return x == v.x && y == v.y;}
	bool operator != (const vec2i& v) const {return x != v.x || y != v.y;}
	bool is_zero() const {return x == 0 && y == 0;}
	
	float len() const {return std::sqrt(x*x + y*y);} ///< Length
	float angle() const {return y && x? std::atan2( y, x ) : 0;} ///< Rotation angle (radians, [-pi, +pi])
	uint len_squ() const {return x*x + y*y;} ///< Square of length
	
	uint ilen() const {return isqrt(x*x + y*y);} ///< Integer length (approximate)
	
	float dist(const vec2i& v) const {return (*this - v).len();} ///< Straight distance
	uint ndg_dist(const vec2i& v) const {return std::abs(x - v.x) + std::abs(y - v.y);} ///< Manhattan distance
	uint int_dist(const vec2i& v) const {return (*this - v).ilen();} ///< Straight integer distance (approximate)
	
	vec2i& rot90cw()  {int t = x; x = y; y = -t; return *this;}
	vec2i& rot90ccw() {int t = x; x = -y; y = t; return *this;}
	
	vec2fp rotate (double angle) const; ///< Rotation by angle (radians)
	vec2fp fastrotate (float angle) const; ///< Rotation by angle (radians) using table lookup

	int area() const {return std::abs(x * y);}
	int perimeter() const {return (x+y)*2;}
	vec2i minmax() const; ///< (min, max)
	double xy_ratio() const {return double(x) / y;}
	
	template <typename T> int& operator() (T) = delete;
	int& operator() (bool is_x) {return is_x? x : y;}
	
	operator vec2fp() const;
	vec2fp get_norm() const;
};

inline vec2i abs(const vec2i& p) {return {std::abs(p.x), std::abs(p.y)};}

inline vec2i operator * (double f, const vec2i& v) {return vec2i(std::floor(v.x * f), std::floor(v.y * f));}

inline vec2i min(const vec2i& a, const vec2i& b) {return {std::min(a.x, b.x), std::min(a.y, b.y)};}
inline vec2i max(const vec2i& a, const vec2i& b) {return {std::max(a.x, b.x), std::max(a.y, b.y)};}

inline bool any_gtr(const vec2i& p, const vec2i& ref) {return p.x > ref.x || p.y > ref.y;}

/// Returns true if point lies in polygon or on it's edge
bool is_in_polygon(vec2i p, const vec2i* ps, size_t pn);

/// Returns true if point lies inside rectangle ({0,0}, size - {1,1}), including edges
bool is_in_bounds(const vec2i& p, const vec2i& size);



/// 2D floating-point vector
struct vec2fp {
	float x, y;
	
	vec2fp() = default;
	vec2fp(float x, float y): x(x),y(y) {}
	void set(float x, float y) {this->x = x; this->y = y;}
	void zero() {x = y = 0;}
	static vec2fp one( float v ) { return {v, v};}
	
	void operator += (const vec2fp& v) {x += v.x; y += v.y;}
	void operator -= (const vec2fp& v) {x -= v.x; y -= v.y;}
	void operator *= (const vec2fp& v) {x *= v.x; y *= v.y;}
	void operator /= (const vec2fp& v) {x /= v.x; y /= v.y;}
	void operator *= (float f) {x *= f; y *= f;}
	void operator /= (float f) {x /= f; y /= f;}
	
	vec2fp operator + (const vec2fp& v) const {return {x + v.x, y + v.y};}
	vec2fp operator - (const vec2fp& v) const {return {x - v.x, y - v.y};}
	vec2fp operator * (const vec2fp& v) const {return {x * v.x, y * v.y};}
	vec2fp operator / (const vec2fp& v) const {return {x / v.x, y / v.y};}
	vec2fp operator * (float f) const {return {x * f, y * f};}
	vec2fp operator / (float f) const {return {x / f, y / f};}
	
	vec2fp operator - () const {return vec2fp(-x, -y);}
	
	bool equals(const vec2fp& v, float eps) const { return aequ(x, v.x, eps) && aequ(y, v.y, eps); }
	bool is_zero(float eps) const {return std::fabs(x) < eps && std::fabs(y) < eps;}
	bool is_exact_zero() const {return x == 0.f && y == 0.f;}
	
	float fastlen() const {return 1.f / fast_invsqrt(x*x + y*y);} ///< Length should be non-zero!
	float len() const {return std::sqrt(x*x + y*y);} ///< Length
	float len_squ() const {return x*x + y*y;} ///< Squared length
	
	float fastangle() const; ///< Approximate rotation angle (radians, [-pi, +pi])
	float angle() const; ///< Rotation angle (radians, [-pi, +pi])
	
	float dist(const vec2fp& v) const {return (*this - v).len();} ///< Straight distance
	float ndg_dist(const vec2fp& v) const {return std::fabs(x - v.x) + std::fabs(y - v.y);} ///< Manhattan distance
	float dist_squ(const vec2fp& v) const {return (*this - v).len_squ();} ///< Squared distance
	
	vec2fp& rot90cw()  {float t = x; x = y; y = -t; return *this;}
	vec2fp& rot90ccw() {float t = x; x = -y; y = t; return *this;}
	
	vec2fp& rotate(double angle); ///< Rotation by angle (radians)
	vec2fp& fastrotate(float angle); ///< Rotation by angle (radians) using table lookup
	vec2fp& rotate(float cos, float sin);
	vec2fp get_rotated(double angle) const {return vec2fp(*this).rotate(angle);}
	
	vec2fp& norm(); ///< Normalizes vector
	vec2fp& norm_to(float n); ///< Brings vector to specified length
	vec2fp& limit_to(float n); ///< Brings vector to specified length if it exceeds it
	vec2fp get_norm() const {return vec2fp(*this).norm();}
	
	float area() const {return std::fabs(x * y);}
	vec2fp minmax() const; ///< (min, max)
	
	vec2i int_floor() const {return vec2i(std::floor(x), std::floor(y));}
	vec2i int_round() const {return vec2i(std::round(x), std::round(y));}
	vec2i int_ceil () const {return vec2i(std::ceil (x), std::ceil (y));}
	
	template <typename T> int& operator() (T) = delete;
	float& operator() (bool is_x) {return is_x? x : y;}
};

inline vec2fp abs(const vec2fp& p) {return {std::abs(p.x), std::abs(p.y)};}

inline float dot  (const vec2fp& a, const vec2fp& b) {return a.x * b.x + a.y * b.y;}
inline float cross(const vec2fp& a, const vec2fp& b) {return a.x * b.y - a.y * b.x;}

inline vec2fp operator * (double f, const vec2fp& v) {return vec2fp(v.x * f, v.y * f);}

/// Spherical linear interpolation of unit vectors
vec2fp slerp (const vec2fp &v0, const vec2fp &v1, float t);

inline vec2fp min(const vec2fp &a, const vec2fp &b) {return {std::min(a.x, b.x), std::min(a.y, b.y)};}
inline vec2fp max(const vec2fp &a, const vec2fp &b) {return {std::max(a.x, b.x), std::max(a.y, b.y)};}

/// Returns line segment intersection point, if any
std::optional<vec2fp> lineseg_intersect(vec2fp a1, vec2fp a2, vec2fp b1, vec2fp b2, float eps = 1e-10);

/// Returns t,u of intersection point a+at*t = b+bt*u if lines aren't collinear or parallel
std::optional<std::pair<float, float>> line_intersect_t(vec2fp a, vec2fp at, vec2fp b, vec2fp bt, float eps = 1e-10);

/// Returns 't' (C = A + t*(B-A)) - point together with 'p' forming perpendicular to line a-b
float lineseg_perpen_t(vec2fp a, vec2fp b, vec2fp p);

/// Returns point which together with 'p' forms perpendicular to line a-b, if it lies in segment bounds
std::optional<vec2fp> lineseg_perpen(vec2fp a, vec2fp b, vec2fp p);

/// Calculates scale and offset to fit rectangle of one size into another, keeping aspect ratio
std::pair<float, vec2fp> fit_rect(vec2fp size, vec2fp into);



/// Integer rectangle
struct Rect {
	vec2i off, sz;
	
	Rect() = default;
	static Rect bounds(vec2i lower, vec2i upper) {return {lower, upper - lower};}
	static Rect off_size(vec2i offset, vec2i size) {return {offset, size};}
	void zero() {off.zero(); sz.zero();}
	
	const vec2i& lower() const {return off;}
	      vec2i  upper() const {return off + sz;}
	const vec2i&  size() const {return sz;}
	      vec2i center() const {return off + sz /2;}
		  vec2fp fp_center() const {return vec2fp(off) + vec2fp(sz) /2;}
	
	void lower(vec2i v) {off = v;}
	void upper(vec2i v) {sz = v - off;}
	void size (vec2i v) {sz = v;}
	
	static Rect from_center_le(vec2i ctr, vec2i half_size) {return bounds(ctr - half_size, ctr + half_size + vec2i::one(1));}
	Rectfp to_fp(float mul) const;
	
	void shift(vec2i v) {off += v;}
	void enclose(vec2i v) {off = min(v, off); upper(max(upper(), v + vec2i::one(1)));}
	vec2i maxpt() const {return off + sz - vec2i::one(1);} ///< Maximum enclosed point
	
	bool empty() const {return sz.x <= 0 || sz.y <= 0;} ///< Returns true if rectangle is of zero size
	bool intersects(const Rect& r) const; ///< Checks if rectangles overlap, including edges
	bool contains(vec2i p) const; ///< Checks if point is inside, including edges
	bool contains_le(vec2i p) const; ///< Checks if point is inside, including only lower edges
	
	operator Rectfp() const;
	operator SDL_Rect() const;
	void set(const SDL_Rect& r);
	
	bool operator == (const Rect& r) const {return lower() == r.lower() && sz == r.sz;}
	bool operator != (const Rect& r) const {return lower() != r.lower() || sz != r.sz;}
	
	/// Maps function over entire area, scanline-like. 
	/// Lower edge included, upper excluded.
	void map(callable_ref<void(vec2i p)> f) const;
	
	/// Same as map, but returns false as soon as 'f' does
	bool map_check(callable_ref<bool(vec2i p)> f) const;
	
	/// Maps function over outer border (-1 from lower and ON upper). 
	/// Clockwise, from lower corner
	void map_outer(callable_ref<void(vec2i p)> f) const;
	
	/// Maps function over inner border (on lower and -1 from upper). 
	/// Clockwise, from lower corner
	void map_inner(callable_ref<void(vec2i p)> f) const;
	
private:
	Rect(vec2i off, vec2i sz): off(off), sz(sz) {}
};

Rect calc_intersection(const Rect& a, const Rect& b); ///< Returns rectangle representing overlap
Rect get_bound(const Rect& a, const Rect& b); ///< Returns rectangle enclosing both rectangles
uint min_distance(const Rect& a, const Rect& b); ///< Returns minimal straight distance



/// Floating-point rectangle
struct Rectfp
{
	vec2fp a, b; // lower and upper bounds
	
	Rectfp() = default;
	static Rectfp bounds(vec2fp lower, vec2fp upper) {return {lower, upper};}
	static Rectfp off_size(vec2fp offset, vec2fp size) {return {offset, offset + size};}
	void zero() { a.zero(); b.zero(); }
	
	const vec2fp& lower() const {return a;}
	const vec2fp& upper() const {return b;}
	vec2fp size() const {return b - a;}
	vec2fp center() const {return a + size() / 2;}
	
	void lower (vec2fp v) {a = v;}
	void upper (vec2fp v) {b = v;}
	void size  (vec2fp v) {b = v + a;}
	
	static Rectfp from_center(vec2fp ctr, vec2fp half_size) {return bounds(ctr - half_size, ctr + half_size);}
	void offset(vec2fp v) {a += v; b += v;}
	
	/// Returns points representing same rectangle rotated around center
	std::array <vec2fp, 4> rotate( float cs, float sn ) const;
	
	/// Returns points representing same rectangle rotated around center (uses cossin_ft)
	std::array <vec2fp, 4> rotate_fast( float rad ) const;
	
	void merge( const Rectfp& r ); ///< Expands this rectangle to enclose another
	bool overlaps( const Rectfp& r ) const; ///< Checks if rectangles overlap, including edges
	bool contains( const Rectfp& r ) const; ///< Checks if another rectangle is completely within this
	
	bool contains( vec2fp p ) const; ///< Checks if point is inside, excluding edges
	bool contains( vec2fp p, float width ) const; ///< Checks if point is inside; edges are shrinked by width
	
private:
	Rectfp(vec2fp a, vec2fp b): a(a), b(b) {}
};



/// 2D
struct Transform
{
	vec2fp pos;
	float rot;
	
	Transform() = default;
	explicit Transform(vec2fp pos, float rot = 0.f): pos(pos), rot(rot) {}
	
	vec2fp apply(vec2fp p) const; ///< Applies transform to point
	vec2fp reverse(vec2fp p) const; ///< Applies reverse transform to point
	
	Transform& combine(const Transform& t);
	Transform& combine_reversed(const Transform& t);
	Transform get_combined(const Transform& t) const;
	
	Transform& add(const Transform& t);
	Transform get_add(const Transform& t) const;
	
	Transform operator -() const {return Transform{-pos, -rot};}
	
	Transform  operator *  (float t) const;
	Transform& operator *= (float t);
	
	Transform  operator /  (float t) const;
	Transform& operator /= (float t);
	
	Transform diff(const Transform& t) const; ///< Returns *this - t
};

inline Transform lerp (const Transform &a, const Transform &b, float t) {
	return Transform{lerp(a.pos, b.pos, t), lerp_angle(a.rot, b.rot, t)};}

#endif // VAS_MATH_HPP
