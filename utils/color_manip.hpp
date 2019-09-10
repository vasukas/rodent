#ifndef COLOR_MANIP_HPP
#define COLOR_MANIP_HPP

#include <cinttypes>



/// Floating-point RGBA color
struct FColor
{
	float r, g, b, a;
	
	FColor() = default;
	FColor( float r, float g, float b, float a = 1.f );
	FColor( uint32_t rgba, float mul = 1.f ); ///< Multiplies colors (not alpha)
	
	      float& operator []( int i );       ///< Returns reference to component
	const float& operator []( int i ) const; ///< Returns reference to component
	
	FColor& operator +=( float f ); ///< except alpha
	FColor& operator *=( float f ); ///< except alpha
	
	FColor& operator +=( const FColor& c ); ///< including alpha
	FColor& operator -=( const FColor& c ); ///< including alpha
	
	FColor operator +( float f )         const { auto self = *this; self += f; return self; }
	FColor operator *( float f )         const { auto self = *this; self *= f; return self; }
	
	FColor operator +( const FColor& c ) const { auto self = *this; self += c; return self; }
	FColor operator -( const FColor& c ) const { auto self = *this; self -= c; return self; }
	
	uint32_t to_px() const;
	
	FColor rgb_to_hsv() const;
	FColor hsv_to_rgb() const;
	
	// HSV hue in range [0, 1]
	static constexpr float H_red0    = 0;
	static constexpr float H_yellow  = 1./6;
	static constexpr float H_green   = 2./6;
	static constexpr float H_cyan    = 0.5;
	static constexpr float H_blue    = 4./6;
	static constexpr float H_magenta = 5./6;
	static constexpr float H_red1    = 1;
};

#endif // COLOR_MANIP_HPP
