#ifndef EXT_MATH_HPP
#define EXT_MATH_HPP

#ifdef __unix__ // on Windows it should be defined in project parameters
#define _USE_MATH_DEFINES
#endif

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>

using uint = unsigned int;

struct SDL_Rect;

struct Rect;
struct Rectfp;
struct vec2i;
struct vec2fp;



inline bool aequ(float  v, float  c, float eps)  {return std::fabs(v - c) < eps;} ///< Approximate comparison
inline bool aequ(double v, double c, double eps) {return std::fabs(v - c) < eps;} ///< Approximate comparison

inline float clampf(float x, float min, float max) {return std::max(min, std::min(max, x));}

/// Integer square root
uint isqrt(uint value);

/// Quake III
float fast_invsqrt(float x);

/// 
inline float fast_sqrt(float x) {return 1.f / fast_invsqrt(x);}

/// Clamps to range [0, M_PI*2]
void clamp_angle(double &x);
void clamp_angle(float  &x);

/// Linear interpolation between two angles, expressed in radians. Handles all cases
inline double lint_angle (double a, double b, double t) {return a + t * std::remainder(b - a, M_PI*2);}

inline float  lint (float  a, float  b, float  t) {return a * (1.f - t) + b * t;}
inline double lint (double a, double b, double t) {return a * (1.0 - t) + b * t;}

vec2fp cossin_ft(float rad); ///< Table-lookup cosine (x) + sine (y)



/// 2D integer vector
struct vec2i {
	int x, y;
	
	vec2i() = default;
	vec2i(int x, int y): x(x),y(y) {}
	void set(int x, int y) {this->x = x, this->y = y;}
	vec2i& zero() {x = y = 0; return *this;}
	static vec2i one( int v ) { return {v, v};}
	
	void operator += (const vec2i& v) {x += v.x, y += v.y;}
	void operator -= (const vec2i& v) {x -= v.x, y -= v.y;}
	void operator *= (const vec2i& v) {x *= v.x, y *= v.y;}
	void operator /= (const vec2i& v) {x /= v.x, y /= v.y;}
	void operator *= (double f) {x *= f, y *= f;}
	void operator /= (double f) {x /= f, y /= f;}
	
	vec2i operator + (const vec2i& v) const {return {x + v.x, y + v.y};}
	vec2i operator - (const vec2i& v) const {return {x - v.x, y - v.y};}
	vec2i operator * (const vec2i& v) const {return {x * v.x, y * v.y};}
	vec2i operator / (const vec2i& v) const {return {x / v.x, y / v.y};}
	vec2i operator * (double f) const {return vec2i(x * f, y * f);}
	vec2i operator / (double f) const {return vec2i(x / f, y / f);}
	
	vec2i operator - () const {return vec2i(-x, -y);}
	
	bool operator == (const vec2i& v) const {return x == v.x && y == v.y;}
	bool operator != (const vec2i& v) const {return x != v.x || y != v.y;}
	
	float len() const {return std::sqrt(x*x + y*y);} ///< Length
	float angle() const {return std::atan2( y, x );} ///< Rotation angle (radians)
	uint len2() const {return x*x + y*y;} ///< Square of length
	
	uint ilen() const {return isqrt(x*x + y*y);} ///< Integer length (approximate)
	
	float dist(const vec2i& v) const {return (*this - v).len();} ///< Straight distance
	uint ndg_dist(const vec2i& v) const {return std::abs(x - v.x) + std::abs(y - v.y);} ///< Manhattan distance
	uint int_dist(const vec2i& v) const {return (*this - v).ilen();} ///< Straight integer distance (approximate)
	
	void rot90cw()  {int t = x; x = y; y = -t;}
	void rot90ccw() {int t = x; x = -y; y = t;}
	
	vec2i get_rotated (double angle) const; ///< Rotation by angle (radians)
	void rotate (double angle); ///< Rotation by angle (radians)
	void fastrotate (float angle); ///< Rotation by angle (radians) using table functions

	int area() const {return std::abs(x * y);}
	
	template <typename T> int& operator() (T) = delete;
	int& operator() (bool is_x) {return is_x? x : y;}
	
	operator vec2fp() const;
	vec2fp get_norm() const;
};

inline vec2i operator * (double f, const vec2i& v) {return vec2i(std::floor(v.x * f), std::floor(v.y * f));}
inline vec2i lint (const vec2i& a, const vec2i& b, double t) {return a * (1. - t) + b * t;}

inline vec2i min(const vec2i& a, const vec2i& b) {return {std::min(a.x, b.x), std::min(a.y, b.y)};}
inline vec2i max(const vec2i& a, const vec2i& b) {return {std::max(a.x, b.x), std::max(a.y, b.y)};}

inline bool any_gtr(const vec2i& p, const vec2i& ref) {return p.x > ref.x || p.y > ref.y;}



/// 2D floating-point vector
struct vec2fp {
	float x, y;
	
	vec2fp() = default;
	vec2fp(float x, float y): x(x),y(y) {}
	void set(float x, float y) {this->x = x, this->y = y;}
	void zero() {x = y = 0;}
	static vec2fp one( float v ) { return {v, v};}
	
	void operator += (const vec2fp& v) {x += v.x, y += v.y;}
	void operator -= (const vec2fp& v) {x -= v.x, y -= v.y;}
	void operator *= (const vec2fp& v) {x *= v.x, y *= v.y;}
	void operator /= (const vec2fp& v) {x /= v.x, y /= v.y;}
	void operator *= (double f) {x *= f, y *= f;}
	void operator /= (double f) {x /= f, y /= f;}
	
	vec2fp operator + (const vec2fp& v) const {return {x + v.x, y + v.y};}
	vec2fp operator - (const vec2fp& v) const {return {x - v.x, y - v.y};}
	vec2fp operator * (const vec2fp& v) const {return {x * v.x, y * v.y};}
	vec2fp operator / (const vec2fp& v) const {return {x / v.x, y / v.y};}
	vec2fp operator * (double f) const {return vec2fp(x * f, y * f);}
	vec2fp operator / (double f) const {return vec2fp(x / f, y / f);}
	
	vec2fp operator - () const {return vec2fp(-x, -y);}
	
	bool equals(const vec2fp& v, float eps) const { return aequ(x, v.x, eps) && aequ(y, v.y, eps); }
	
	float fastlen() const {return 1.f / fast_invsqrt(x*x + y*y);} ///< Length should be non-zero!
	float len() const {return std::sqrt(x*x + y*y);} ///< Length
	float angle() const {return std::atan2(y, x);} ///< Rotation angle (radians)
	float len2() const {return x*x + y*y;} ///< Square of length
	
	float dist(const vec2fp& v) const {return (*this - v).len();} ///< Straight distance
	float ndg_dist(const vec2fp& v) const {return std::fabs(x - v.x) + std::fabs(y - v.y);} ///< Manhattan distance
	
	void rot90cw()  {float t = x; x = y; y = -t;}
	void rot90ccw() {float t = x; x = -y; y = t;}
	
	vec2fp get_rotated (double angle) const; ///< Rotation by angle (radians)
	void rotate (double angle); ///< Rotation by angle (radians)
	void fastrotate (float angle); ///< Rotation by angle (radians) using table functions

	vec2fp get_rotate (float cos, float sin);
	void rotate (float cos, float sin);
	
	vec2fp get_norm() const; ///< Returns normalized vector
	void norm(); ///< Normalizes vector
	
	float area() const {return std::fabs(x * y);}
	
	vec2i int_floor() const {return vec2i(std::floor(x), std::floor(y));}
	vec2i int_round() const {return vec2i(std::round(x), std::round(y));}
	vec2i int_ceil () const {return vec2i(std::ceil (x), std::ceil (y));}
	
	float dot( const vec2fp& v ) const {return x * v.x + y * v.y;}
};

inline vec2fp operator * (double f, const vec2fp& v) {return vec2fp(v.x * f, v.y * f);}
inline vec2fp lint (const vec2fp &a, const vec2fp &b, double t) {return a * (1. - t) + b * t;}

inline vec2fp min(const vec2fp &a, const vec2fp &b) {return {std::min(a.x, b.x), std::min(a.y, b.y)};}
inline vec2fp max(const vec2fp &a, const vec2fp &b) {return {std::max(a.x, b.x), std::max(a.y, b.y)};}



/// Integer upper/size-agnostic rectangle
struct Rect {
	Rect() = default;
	Rect(int ax, int ay, int bx, int by): Rect({ax, ay}, {bx, by}, true) {}
	Rect    (vec2i p0, vec2i b, bool is_size) {this->p0 = p0, this->b = b; this->is_size = is_size;}
	void set(vec2i p0, vec2i b, bool is_size) {this->p0 = p0, this->b = b; this->is_size = is_size;}
	void zero() {p0.zero(); b = p0; is_size = false;}
	
	const vec2i& lower() const {return p0;}
	vec2i upper() const {return is_size? b + p0 : b;}
	vec2i size()  const {return is_size? b : b - p0;}
	
	void lower(vec2i v) {if (is_size) b -= v - p0; p0 = v;}
	void upper(vec2i v) {b = v; is_size = false;}
	void size (vec2i v) {b = v; is_size = true;}
	
	void shift(vec2i v) {if (!is_size) b += v; p0 += v;}
	vec2i& raw_a() {return p0;}
	void enclose(vec2i v) {p0 = min(v, p0); upper(max(upper(), v + vec2i::one(1)));}
	
	bool empty() const {return size() == vec2i();} ///< Returns true if rectangle is of zero size
	bool intersects(const Rect& r) const; ///< Checks if rectangles overlap, including edges
	bool contains(vec2i p) const; ///< Checks if point is inside, including edges
	
	operator Rectfp() const;
	operator SDL_Rect() const;
	void set(const SDL_Rect& r);
	
	bool operator == (const Rect& r) const {return lower() == r.lower() && size() == r.size();}
	bool operator != (const Rect& r) const {return lower() != r.lower() || size() != r.size();}
	
private:
	vec2i p0, b;
	bool is_size;
};

Rect calc_intersection(const Rect& a, const Rect& b); ///< Returns rectangle representing overlap
Rect get_bound(const Rect& a, const Rect& b); ///< Returns rectangle enclosing both rectangles



/// Floating-point rectangle
struct Rectfp
{
	vec2fp a, b; // lower and upper bounds
	
	Rectfp() = default;
	Rectfp( int ax, int ay, int bx, int by ) {a.set( ax, ay ), b.set( ax + bx, ay + by );}
	Rectfp  (vec2fp a, vec2fp b, bool is_size) {this->a = a, this->b = is_size? b + a : b;}
	void set(vec2fp a, vec2fp b, bool is_size) {this->a = a, this->b = is_size? b + a : b;}
	void zero() { a.zero(); b.zero(); }
	
	const vec2fp& lower() const {return a;}
	const vec2fp& upper() const {return b;}
	vec2fp size() const {return b - a;}
	vec2fp center() const {return a + size() / 2;}
	
	void lower (vec2fp v) {a = v;}
	void upper (vec2fp v) {b = v;}
	void size  (vec2fp v) {b = v + a;}
	
	/// Returns points representing same rectangle rotated around center
	std::array <vec2fp, 4> rotate( float cs, float sn ) const;
	
	/// Returns points representing same rectangle rotated around center (uses cossin_ft)
	std::array <vec2fp, 4> rotate_fast( float rad ) const;
	
	void merge( const Rectfp& r ); ///< Expands this rectangle to enclose another
	bool overlaps( const Rectfp& r ) const; ///< Checks if rectangles overlap, including edges
	bool contains( const Rectfp& r ) const; ///< Checks if another rectangle is completely within this
};



/// 2D
struct Transform
{
	vec2fp pos;
	float rot;
	
	Transform() = default;
	Transform(vec2fp pos, float rot = 0.f): pos(pos), rot(rot) {}
	
	vec2fp apply(vec2fp p) const; ///< Applies transform to point
	vec2fp reverse(vec2fp p) const; ///< Applies reverse transform to point
	
	void combine(const Transform& t);
	void combine_reversed(const Transform& t);
	
	Transform get_combined(const Transform& t) const;
	
	void add(const Transform& t);
	
	Transform operator -() const {return Transform{-pos, -rot};}
	Transform operator * (float t) const;
	void operator *= (float t);
};

#endif // EXT_MATH_HPP
