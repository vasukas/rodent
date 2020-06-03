#ifndef RES_IMAGE_HPP
#define RES_IMAGE_HPP

#include <vector>
#include "vaslib/vas_math.hpp"

struct SDL_Surface;



/// Image parameters and data
struct ImageInfo
{
	/// Pixel format
	enum Format
	{
		FMT_ALPHA, ///< Single-channel 8-bit
		FMT_RGB,   ///< TrueColor (24-bit)
		FMT_RGBA   ///< TrueColor with alpha (32-bit)
	};
	
	/// Max bytes per pixel possible
	static constexpr int max_bpp_value = 4;
	
	/// Default is 3
	static int png_compression_level;
	
	/// Returns bytes-per-pixel value for that format
	static int get_bpp( Format fmt );
	
	
	
	/// Creates empty image with FMT_RGBA
	ImageInfo() = default;
	
	/// Loads image from file, converting to specified format if it's set
	bool load( const char* name, std::optional<Format> force_fmt = {} );
	
	/// Saves image to file with current format
	bool save( const char* name ) const;
	
	/// Proxy DOES NOT hold copy of image data. 
	/// Should be destroyed by user. 
	/// Created only for RGB and RGBA image
	SDL_Surface* proxy() const;
	
	/// Loads image if exists, generates and saves if not
	static std::vector<uint8_t> procedural(const char *filename, vec2i size, Format fmt,
	                                       callable_ref<void(ImageInfo&)> generate);
	
	
	/// Clears image (optionally changing format)
	void reset( vec2i new_size, std::optional<Format> new_fmt = {}, bool keep_data = false );
	
	/// Clears image (sets pixels to zero)
	void clear();
	
	/// Changes format
	void convert( Format new_fmt );
	
	/// Flips image vertically
	void vflip();
	
	
	
	/// Swaps pixel data. Format isn't changed
	void px_swap( std::vector<uint8_t>& px, std::optional<vec2i> new_size = {} );
	
	/// Returns moved pixel data. Size set to zero
	std::vector<uint8_t> move_px();
	
	/// Returns raw pixel pointer
	const uint8_t* raw() const { return px.data(); }
	
	/// Returns raw pixel pointer
	uint8_t* raw() { return px.data(); }
	
	/// Returns width and height
	vec2i get_size() const { return size; }
	
	/// Returns pixel format
	Format get_fmt() const { return fmt; }
	
	/// Returns bytes-per-pixel for current format
	int get_bpp() const { return get_bpp( fmt ); }
	
	
	
	/// Returns subimage specified by rectangle. Bounds-safe 
	ImageInfo subimg( Rect r ) const;
	
	/// Returns pixel value without checking bounds
	uint32_t get_pixel_fast( vec2i pos ) const;
	
	/// Sets pixel value without checking bounds
	void set_pixel_fast( vec2i pos, uint32_t v );
	
	/// Returns pointer to pixel without checking bounds
	const uint8_t* get_pixel_ptr_fast( vec2i pos ) const;
	
	/// Returns pointer to pixel without checking bounds
	      uint8_t* get_pixel_ptr_fast( vec2i pos );
		  
	/// Checks if coordinates are valid
	bool is_in_bounds( vec2i pos ) const;
	
	
	
	/// Resizes image keeping contents (crops)
	void resize( vec2i new_size );
	
	/// Copies contents from image of same format. Performs bound check
	void blit( vec2i to, const ImageInfo& from, vec2i src, vec2i size );
	
	/// Calls function with pointer to beginning of each pixel
	void map_pixel(callable_ref<void(uint8_t*)> f);
	
private:
	std::vector <uint8_t> px;
	vec2i size = {};
	Format fmt = FMT_RGBA;
};

#endif // RES_IMAGE_HPP
