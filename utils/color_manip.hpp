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
};

#endif // COLOR_MANIP_HPP
