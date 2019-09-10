#include "utils/res_image.hpp"
#include "vaslib/vas_log.hpp"
#include "gl_utils.hpp"
#include "texture.hpp"

size_t Texture::dbg_total_size = 0;



uint TextureReg::get_obj() const
{
	return tex? tex->get_obj() : 0;
}
vec2i TextureReg::px_size() const
{
	return tex? (tc.size() * tex->get_size()).int_round() : vec2i{};
}



struct FmtDescr
{
	GLint internal;
	GLenum in_format, in_type;
	int bpp;
};
static const FmtDescr fmts[3] =
{
    {GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1},
    {GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, 4},
    {GL_RGB, GL_RGB, GL_UNSIGNED_BYTE, 3}
};
static const FmtDescr* get_fmt(Texture::Format fmt)
{
	return &fmts[static_cast<size_t>(fmt)];
}



static std::optional<Texture::Format> assoc_fmt(ImageInfo::Format fmt)
{
	switch (fmt)
	{
	case ImageInfo::FMT_ALPHA: return Texture::FMT_SINGLE;
	case ImageInfo::FMT_RGB:   return Texture::FMT_RGB;
	case ImageInfo::FMT_RGBA:  return Texture::FMT_RGBA;
	}
	return {};
}
static std::optional<ImageInfo::Format> assoc_fmt(Texture::Format fmt)
{
	switch (fmt)
	{
	case Texture::FMT_SINGLE: return ImageInfo::FMT_ALPHA;
	case Texture::FMT_RGB:    return ImageInfo::FMT_RGB;
	case Texture::FMT_RGBA:   return ImageInfo::FMT_RGBA;
	}
	return {};
}



class Texture_Impl : public Texture
{
public:
	GLuint tex;
	vec2i size;
	const FmtDescr* fmt;
	
	Texture_Impl( vec2i size, const FmtDescr* fmt )
		: size(size), fmt(fmt)
	{
		glGenTextures( 1, &tex );
		dbg_total_size += size.area() * fmt->bpp;
	}
	~Texture_Impl()
	{
		glDeleteTextures( 1, &tex );
		dbg_total_size -= size.area() * fmt->bpp;
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
			
		case FIL_LINEAR_MIPMAP:
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			glGenerateMipmap( GL_TEXTURE_2D );
			break;
		}
	}
	void update( const Rect& part, const void *data )
	{
		auto [x, y] = part.lower();
		auto [w, h] = part.size();
		
		glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
		glBindTexture( GL_TEXTURE_2D, tex );
		glTexSubImage2D( GL_TEXTURE_2D, 0, x, y, w, h, fmt->in_format, fmt->in_type, data );
	}
	void update_full( const void *data, std::optional<Format> new_fmt )
	{
		if (new_fmt)
			fmt = get_fmt(*new_fmt);
		
		glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
		glBindTexture( GL_TEXTURE_2D, tex );
		glTexImage2D( GL_TEXTURE_2D, 0, fmt->internal, size.x, size.y, 0, fmt->in_format, fmt->in_type, data );
	}
};
Texture* Texture::load( const char *filename, Format fmt, Filter fil )
{
	auto i_fmt = assoc_fmt(fmt);
	if (!i_fmt) {
		VLOGE("Texture::load() unsupported format");
		return nullptr;
	}
	
	ImageInfo img;
	if (!img.load(filename, *i_fmt)) {
		VLOGE("Texture::load() failed");
		return nullptr;
	}
	return create_from( img.get_size(), fmt, img.raw(), fil );
}
Texture* Texture::create_empty( vec2i size, Format fmt, Filter fil )
{
	auto f = get_fmt(fmt);
	auto tx = new Texture_Impl( size, f );
	glBindTexture( GL_TEXTURE_2D, tx->tex );
	glTexImage2D( GL_TEXTURE_2D, 0, f->internal, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	tx->set_filter( fil );
	return tx;
}
Texture* Texture::create_from( const ImageInfo& img, Filter fil )
{
	auto fmt = assoc_fmt(img.get_fmt());
	if (!fmt)
		throw std::runtime_error("Texture::create_from() unsupported format");
	
	return create_from( img.get_size(), *fmt, img.raw(), fil );
}
Texture* Texture::create_from( vec2i size, Format fmt, const void *data, Filter fil )
{
	auto f = get_fmt(fmt);
	auto tx = new Texture_Impl( size, f );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glBindTexture( GL_TEXTURE_2D, tx->tex );
	glTexImage2D( GL_TEXTURE_2D, 0, f->internal, size.x, size.y, 0, f->in_format, f->in_type, data );
	tx->set_filter( fil );
	return tx;
}
void Texture::debug_save(uint obj, const char *filename, Format fmt, uint target)
{
	auto i_fmt = assoc_fmt(fmt);
	if (!i_fmt) {
		VLOGE("Texture::debug_save() unsupported format");
		return;
	}
	
	if (!target) target = GL_TEXTURE_2D;
	glBindTexture(target, obj);
	
	int w, h;
	glGetTexLevelParameteriv(target, 0, GL_TEXTURE_WIDTH, &w);
	glGetTexLevelParameteriv(target, 0, GL_TEXTURE_HEIGHT, &h);
	
	ImageInfo img;
	img.reset({w, h}, *i_fmt);
	
	auto f = get_fmt(fmt);
	glGetTexImage(target, 0, f->in_format, GL_UNSIGNED_BYTE, img.raw());
	
	bool ok = img.save(filename);
	VLOGD("Texture::debug_save() {} of {}", ok, filename);
}
