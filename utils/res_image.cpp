#include <SDL2/SDL_surface.h>
#include "vaslib/vas_log.hpp"
#include "res_image.hpp"

#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_NO_HDR
#define STBI_FAILURE_USERMSG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int ImageInfo::png_compression_level = 3;



int ImageInfo::get_bpp( Format fmt )
{
	switch( fmt )
	{
	case FMT_ALPHA: return 1;
	case FMT_RGB:   return 3;
	case FMT_RGBA:  return 4;
	}
	ASSERT( false, "ImageInfo::get_bpp() invalid enum" );
	return 0;
}
bool ImageInfo::load( const char *name, std::optional<Format> force_fmt )
{
	int bpp = 0, force_bpp = (force_fmt ? get_bpp(*force_fmt) : 0);
	uint8_t *data = stbi_load( name, &size.x, &size.y, &bpp, force_bpp );
	if (!data)
	{
		VLOGE("ImageInfo::load() stbi_load failed: \"{}\" - {}", name, stbi_failure_reason());
		return false;
	}
	
	if (!force_fmt)
	{
		if		(bpp == 1) force_fmt = FMT_ALPHA;
		else if (bpp == 3) force_fmt = FMT_RGB;
		else if (bpp == 4) force_fmt = FMT_RGBA;
		else
		{
			VLOGE("ImageInfo::load() invalid image format: \"{}\"", name);
			stbi_image_free( data );
			return false;
		}
	}
	fmt = *force_fmt;

	px.clear();
	px.insert( px.end(), data, data + size.area() * get_bpp( fmt ) );
	stbi_image_free( data );
	
	if (bpp == 3 && force_fmt == FMT_RGBA)
	{
		for (int i = 0; i < size.area(); ++i)
		{
			if (px[i*4] + px[i*4+1] + px[i*4+2] == 255*3)
				px[i*4+3] = 0;
		}
	}
	
	VLOGD("ImageInfo::load() ok: \"{}\"", name);
	return true;
}
bool ImageInfo::save( const char *name ) const
{
	int bpp = get_bpp();
	
	auto ext = strrchr(name, '.');
	if (!ext)
	{
		VLOGD("ImageInfo::save() no file extension: \"{}\"", name);
		return false;
	}

	if		(!strcmp(ext, ".png"))
	{
		stbi_write_png_compression_level = png_compression_level;
		if (!stbi_write_png( name, size.x, size.y, bpp, px.data(), bpp * size.x) )
		{
			VLOGD("ImageInfo::save() stbi_write_png failed: \"{}\" - {}", name, stbi_failure_reason());
			return false;
		}
	}
	else if (!strcmp(ext, ".bmp"))
	{
		if (!stbi_write_bmp( name, size.x, size.y, bpp, px.data()) )
		{
			VLOGD("ImageInfo::save() stbi_write_bmp failed: \"{}\" - {}", name, stbi_failure_reason());
			return false;
		}
	}
	else
	{
		VLOGD("ImageInfo::save() unknown file extension: \"{}\"", name);
		return false;
	}
	
	VLOGD("ImageInfo::save() ok: \"{}\"", name);
	return true;
}
SDL_Surface* ImageInfo::proxy() const
{
	int bpp = get_bpp();
	int sdl_fmt = 0;

	if		(bpp == 4) sdl_fmt = SDL_PIXELFORMAT_RGBA32;
	else if (bpp == 3) sdl_fmt = SDL_PIXELFORMAT_RGB24;
	else
	{
		VLOGX("ImageInfo::proxy() unsupported format - {}", fmt);
		return nullptr;
	}
	
	// data is NOT copied!
	auto ptr = const_cast<void*> (static_cast<const void*> (raw()));
	
	SDL_Surface* sur = SDL_CreateRGBSurfaceWithFormatFrom( ptr, size.x, size.y, bpp * 8, size.x * bpp, sdl_fmt );
	if (!sur) VLOGE("ImageInfo::proxy() SDL_CreateRGBSurfaceWithFormatFrom failed - {}", SDL_GetError());
	return sur;
}
void ImageInfo::reset(vec2i new_size, std::optional<Format> new_fmt, bool keep_data)
{
	if (new_fmt) fmt = *new_fmt;
	size = new_size;
	if (!keep_data) px.clear();
	px.resize( size.area() * get_bpp() );
}
void ImageInfo::clear()
{
	for (auto& p : px) p = 0;
}
void ImageInfo::convert( Format new_fmt )
{
	if (fmt == new_fmt) return;
	
	if (fmt == FMT_ALPHA && new_fmt == FMT_RGBA)
	{
		size_t i = px.size() - 1;
		px.resize( size.area() * get_bpp(new_fmt) );
		for (; i != static_cast<size_t>(-1); --i)
		{
			uint8_t *src = px.data() + i;
			uint8_t *dst = px.data() + i*4;
			for (int i=0; i<3; ++i) dst[i] = *src;
			dst[3] = 255;
		}
	}
	else if (fmt == FMT_RGBA && new_fmt == FMT_RGB)
	{
		int n = size.area();
		for (int i=0; i<n; ++i)
		{
			uint8_t *src = px.data() + i*4;
			uint8_t *dst = px.data() + i*3;
			for (int i=0; i<3; ++i) dst[i] = src[i];
		}
		px.resize( n*3 );
	}
	else throw std::logic_error("ImageInfo::convert() not implemented");
	
	fmt = new_fmt;
}
void ImageInfo::vflip()
{
	int n = size.x * get_bpp();
	for (int y=0; y < size.y /2; ++y)
	{
		uint8_t *a = px.data() + n * y;
		uint8_t *b = px.data() + n * (size.y - y - 1);
		for (int i=0; i<n; ++i) std::swap(a[i], b[i]);
	}
}
void ImageInfo::px_swap(std::vector<uint8_t>& px , std::optional<vec2i> new_size)
{
	this->px.swap(px);
	if (new_size) size = *new_size;
}
std::vector<uint8_t> ImageInfo::move_px()
{
	size = {};
	return std::move(px);
}
ImageInfo ImageInfo::subimg( Rect r ) const
{
	int bpp = get_bpp();
	vec2i low = r.lower();
	vec2i upp = r.upper();
	
	low = max( {0,0}, min( size, low ) );
	upp = max( {0,0}, min( size, upp ) );
	vec2i nz = upp - low;
	
	if (nz.x <= 0 || nz.y <= 0)
		return {};
	
	ImageInfo ii;
	ii.reset( nz, fmt );
	
	for (int y = 0; y < nz.y; ++y)
	{
		auto src = px.data() + ((y + low.y) * size.x + low.x) * bpp;
		memcpy( ii.px.data() + y * nz.x * bpp, src, nz.x * bpp );
	}
	
	return ii;
}
uint32_t ImageInfo::get_pixel_fast( vec2i pos ) const
{
	auto off = get_pixel_ptr_fast( pos );
	int bpp = get_bpp();
	uint32_t v = 0;
	
	for (int i = 0; i < bpp; ++i)
	{
		v <<= 8;
		v |= off[ i ];
	}
	return v;
}
void ImageInfo::set_pixel_fast( vec2i pos, uint32_t v )
{
	auto off = get_pixel_ptr_fast( pos );
	int bpp = get_bpp();
	
	for (int i = bpp - 1; i >= 0; --i)
	{
		off[ i ] = v;
		v >>= 8;
	}
}
uint8_t* ImageInfo::get_pixel_ptr_fast( vec2i pos )
{
	return px.data() + (pos.y * size.x + pos.x) * get_bpp();
}
const uint8_t* ImageInfo::get_pixel_ptr_fast( vec2i pos ) const
{
	return px.data() + (pos.y * size.x + pos.x) * get_bpp();
}
bool ImageInfo::is_in_bounds( vec2i pos ) const
{
	return ::is_in_bounds(pos, size);
}
void ImageInfo::resize( vec2i new_size )
{
	int bpp = get_bpp();
	std::vector< uint8_t > old = px;
	
	px.clear();
	px.resize( new_size.area() * bpp );
	
	for (int y = 0; y < std::min( new_size.y, size.y ); ++y)
	{
		int n = bpp * std::min( new_size.x, size.x );
		memcpy( px.data() + y * new_size.x * bpp, old.data() + y * size.x * bpp, n );
	}
	size = new_size;
}
void ImageInfo::blit( vec2i dst, const ImageInfo& from, vec2i src, vec2i sz )
{
	ASSERT( fmt == from.fmt, "ImageInfo::blit() format mismatch" );
	int bpp = get_bpp();
	
	for (int y = 0; y < sz.y; ++y)
	for (int x = 0; x < sz.x; ++x)
	{
		int s_y = y + src.y; if (s_y < 0 || s_y >= from.size.y) continue;
		int s_x = x + src.x; if (s_x < 0 || s_x >= from.size.x) continue;
		
		int d_y = y + dst.y; if (d_y < 0 || d_y >= size.y) continue;
		int d_x = x + dst.x; if (d_x < 0 || d_x >= size.x) continue;
		
		memcpy( px.data() + (d_y *      size.x + d_x) * bpp,
		   from.px.data() + (s_y * from.size.x + s_x) * bpp, bpp );
	}
}
void ImageInfo::map_pixel(callable_ref<void(uint8_t *)> f)
{
	for (size_t i=0; i<px.size(); i += get_bpp())
		f(px.data() + i);
}


std::vector<uint8_t> ImageInfo::procedural(const char *filename, vec2i size, Format fmt,
	                                       callable_ref<void(ImageInfo&)> generate)
{
	ImageInfo img;
	bool ok = img.load(filename, fmt);
	
	if (!ok) VLOGW("ImageInfo::procedural() failed to load \"{}\"", filename);
	else if (img.get_size() != size) {
		VLOGW("ImageInfo::procedural() incompatible size \"{}\"", filename);
		ok = false;
	}
	
	if (!ok) {
		img.reset(size, fmt);
		generate(img);
		if (!img.save(filename))
			VLOGE("ImageInfo::procedural() failed to save \"{}\"", filename);
	}
	return img.move_px();
}
