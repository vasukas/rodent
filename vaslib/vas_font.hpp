// FreeType2 font loader

#ifndef VAS_FONT_HPP
#define VAS_FONT_HPP

#include <cinttypes>
#include <vector>
#include "vas_math.hpp"

#define VAS_HAS_FREETYPE	1



class File;

namespace vas {



/// Font loader (provides general font info and renders glyphs)
class Font {
public:
	/// Kerning info
	struct KernPair
	{
		char32_t cp; ///< codepoint of next symbol
		vec2i off; ///< kerning offset
	};
	
	struct Glyph
	{
		char32_t cp;
		
		vec2i off; ///< drawing offset
		int xadv; ///< horizontal advance
		std::vector <KernPair> kerns;
		
		std::vector <uint8_t> image; ///< 8-bit alpha; empty if glyph not rendered
		vec2i size; ///< size of image
		
		
		/// Returns kerning offset to next codepoint
		vec2i get_kern (char32_t next);
	};
	
	
	
#if VAS_HAS_FREETYPE == 1
	/// Intializes FreeType2 library and increases reference count
	static bool load_ft_lib();
	
	/// Deinitializes FreeType2 library (decrements refcount)
	static bool unload_ft_lib();
	
	/// Loads font using FreeType2 with specified size (height) and font index. 
	/// Note: loads library if needed
	static Font* load_ft(const char* filename, float pt, uint32_t index = 0);
	
	/// Loads font using FreeType2 from memory
	static Font* load_ft(const void *mem, size_t size, float pt);
#endif
	
	/// Loads pre-rendererd bitmap font in special format
	static Font* load_vas(File& f);
	
	/// Saves rendered font to special format, returns false on any error. Currently supports only monowide fonts
	bool save_vas(File& f);

	/// Closes font file
	virtual ~Font() {}
	
	
	
	/// Checks if font contains glyph
	virtual bool has_glyph(char32_t cp) = 0;
	
	/// Loads glyph info and renders it.
	/// Returns false if rendering failed.
	/// If font doesn't contain glyph, it's rendered as 'missed glyph' and 'true' is returned
	virtual bool load_glyph(char32_t cp) = 0;
	
	/// Loads and renders all existing (!) glyphs from range (including).
	/// Returns number of glyphs loaded (extisting and successfully rendered)
	virtual int load_glyphs(char32_t cp_first, char32_t cp_last);
	
	/// Unloads glyph info
	virtual void unload_glyph(char32_t cp) = 0;
	
	/// Unloads all glyphs in range, including
	virtual void unload_glyphs(char32_t cp_first, char32_t cp_last);
	
	/// Arg for update_info()
	struct Info
	{
		bool kerning = true; ///< Input: load kerning info; Output: was any kerning info loaded
		int width; ///< Output: glyph width if monowide or 0 if not
		int line; ///< Output: line height (offset)
		
		/// Input: 1 to calculate width mode (most used value) if not monowide.
		/// Output: mode of character width
		int mode = 0;
	};
	
	/// Updates font/glyph information/settings
	virtual void update_info(Info &info) = 0;
	
	/// Unloads all glyphs
	virtual void clear_glyphs() = 0;
	
	
	
	/// Returns loaded glyph or nullptr if not found.
	/// Pointer is owned internally and may become invalid after any non-const call
	virtual const Glyph* get_glyph(char32_t cp) const = 0;
	
	/// Returns all loaded glyphs
	virtual std::vector<Glyph> get_glyphs() const = 0;
	
	/// Checks if font more or less monowide. Tolerance is threshold after which char considered non-monowide,
	/// threshold is percentage of such chars below which font still considered monowide. 
	/// Note: glyphs must be loaded
	virtual bool monowide_check(int tolerance, int threshold);
};

}

#endif // VAS_FONT_HPP
