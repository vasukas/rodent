#ifndef TEXTURE_HPP
#define TEXTURE_HPP

#include "vaslib/vas_math.hpp"

struct ImageInfo;
class  Texture;



/// Represents region of texture for drawing
struct TextureReg
{
	Texture* tex = nullptr;
	Rectfp tc; ///< Texture coordinates, [0-1]
	
	uint get_obj() const; ///< Returns texture object or 0
};



/// Wrapper for GL texture
class Texture
{
public:
	enum Format
	{
		FMT_SINGLE, ///< Single-channel 8-bit texture. Can be read as red in shaders
		FMT_RGBA    ///< 32-bit
	};
	enum Filter
	{
		FIL_NEAREST, ///< Only magnification, minification is linear
		FIL_LINEAR
	};
	
	static size_t dbg_total_size; ///< Shows how much raw pixel data of all existing textures takes, in bytes
	
	static Texture* create_empty( vec2i size, Format fmt, Filter fil = FIL_LINEAR );
	static Texture* create_from( const ImageInfo& img, Filter fil = FIL_LINEAR );
	static Texture* create_from( vec2i size, Format fmt, const void *data, Filter fil = FIL_LINEAR );
	virtual ~Texture() = default;
	
	/// Returns real texture object
	virtual uint get_obj() const = 0;
	
	/// Returns texture dimensions
	virtual vec2i get_size() const = 0;
	
	/// Converts pixels to texture coordinates [0-1]
	virtual vec2fp to_texcoord( vec2i p ) const = 0;
	
	/// Converts pixels to texture coordinates [0-1]
	virtual Rectfp to_texcoord( Rect p ) const = 0;
	
	/// Sets filtering
	virtual void set_filter( Filter fil ) = 0;
	
	/// This function is slow and unsafe
	virtual void update( const Rect& part, const void *data ) = 0;
};

#endif // TEXTURE_HPP
