#include "core/res_image.hpp"
#include "vaslib/vas_log.hpp"
#include "gl_utils.hpp"
#include "texture.hpp"


size_t Texture::dbg_total_size = 0;

uint TextureReg::get_obj() const
{
	return tex? tex->get_obj() : 0;
}



struct FmtDescr
{
	GLint internal;
	GLenum in_format, in_type;
	int bpp;
};
static const FmtDescr fmts[2] =
{
    {GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1},
    {GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, 4}
};



class Texture_Impl : public Texture
{
public:
	GLuint tex;
	vec2i size;
	FmtDescr fmt;
	
	Texture_Impl( vec2i size, FmtDescr fmt )
		: size(size), fmt(fmt)
	{
		glGenTextures( 1, &tex );
		dbg_total_size += size.area() * fmt.bpp;
	}
	~Texture_Impl()
	{
		glDeleteTextures( 1, &tex );
		dbg_total_size -= size.area() * fmt.bpp;
	}
	uint get_obj() const
	{
		return tex;
	}
	vec2i get_size() const
	{
		return size;
	}
	vec2fp to_texcoord( vec2i p ) const
	{
		return vec2fp(p) / size;
	}
	Rectfp to_texcoord( Rect p ) const
	{
		return
		{
			vec2fp( p.lower() ) / size,
			vec2fp( p.upper() ) / size,
			false
		};
	}
	void set_filter( Filter fil )
	{
		glBindTexture( GL_TEXTURE_2D, tex );
		switch( fil )
		{
		case FIL_NEAREST:
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
			break;
			
		case FIL_LINEAR:
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			break;
		}
	}
	void update( const Rect& part, const void *data )
	{
		auto [x, y] = part.lower();
		auto [w, h] = part.size();
		
		glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
		glBindTexture( GL_TEXTURE_2D, tex );
		glTexSubImage2D( GL_TEXTURE_2D, 0, x, y, w, h, fmt.in_format, fmt.in_type, data );
	}
};
Texture* Texture::create_empty( vec2i size, Format fmt, Filter fil )
{
	FmtDescr f = fmts[fmt];
	auto t = new Texture_Impl( size, f );
	glBindTexture( GL_TEXTURE_2D, t->tex );
	glTexImage2D( GL_TEXTURE_2D, 0, f.internal, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	t->set_filter( fil );
	return t;
}
Texture* Texture::create_from( const ImageInfo& img, Filter fil )
{
	Format fmt;
	if		(img.get_fmt() == ImageInfo::FMT_ALPHA) fmt = FMT_SINGLE;
	else if (img.get_fmt() == ImageInfo::FMT_RGBA)  fmt = FMT_RGBA;
	else
	{
		VLOGE( "Texture::create_from() unsupported format" );
		return nullptr;
	}
	return create_from( img.get_size(), fmt, img.raw(), fil );
}
Texture* Texture::create_from( vec2i size, Format fmt, const void *data, Filter fil )
{
	FmtDescr f = fmts[fmt];
	auto t = new Texture_Impl( size, f );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glBindTexture( GL_TEXTURE_2D, t->tex );
	glTexImage2D( GL_TEXTURE_2D, 0, f.internal, size.x, size.y, 0, f.in_format, f.in_type, data );
	t->set_filter( fil );
	return t;
}
