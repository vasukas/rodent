#include "vas_file.hpp"
#include "vas_font.hpp"
#include "vas_log.hpp"
using namespace vas;



Font* Font::load_auto(const char *filename, float pt)
{
	std::string ext = strrchr(filename, '.');
	for (auto& c : ext) c = tolower(c);
	
#if VAS_HAS_FREETYPE == 1
	return load_ft(filename, pt);
#else
	VLOGE("Font::load() unknown file format - \"{}\"", filename);
	return nullptr;
#endif
}
vec2i Font::Glyph::get_kern (char32_t next)
{
	for (auto &k : kerns) if (k.cp == next) return k.off;
	return {};
}
int Font::load_glyphs(char32_t cp_first, char32_t cp_last)
{
	int n = 0;
	for (; cp_first <= cp_last; ++cp_first)
		if (has_glyph(cp_first) && load_glyph(cp_first)) ++n;
	return n;
}
void Font::unload_glyphs(char32_t cp_first, char32_t cp_last)
{
	for (; cp_first <= cp_last; ++cp_first)
		unload_glyph(cp_first);
}
bool Font::monowide_check(int tolerance, int threshold)
{
	vas::Font::Info inf;
	inf.kerning = false;
	inf.mode = 1;
	update_info(inf);
	
	size_t other = 0;
	auto gs = get_glyphs();
	for (auto& fg : gs) {
		if (abs(fg.xadv - inf.mode) > tolerance) ++other;
	}
	
	if (other != 0) {
		int perc = 100 * other / gs.size();
		if (perc < threshold) {
			VLOGD("Font::monowide_check() ok, below threshold ({}% vs {}%)", perc, threshold);
			return true;
		}
		VLOGD("Font::monowide_check() fail, above threshold ({}% vs {}%)", perc, threshold);
		return false;
	}
	
	VLOGD("Font::monowide_check() ok");
	return true;
}



#if VAS_HAS_FREETYPE == 1

#ifndef _WIN32
#include <freetype2/ft2build.h>
#else
#include "ft2build.h" // otherwise doesn't compile with custom buildscript
#endif
#include FT_FREETYPE_H



static const char *ft_error_string (int code)
{
#undef FTERRORS_H_ 
#define FT_ERROR_START_LIST	  switch (code) {
#define FT_ERRORDEF(e, v, s)  case e: return s;
#define FT_ERROR_END_LIST     }
	#include FT_ERRORS_H
	return "Unknown error";
}
#define FTERR(e) ft_error_string(e)



// All sizes in FreeType are in 1/64th of pixel
#define PX_TO_FT(PX) ((PX) * 64)
#define FT_TO_PX(FT) round((FT) / 64.)

/// FreeType font wrapper
class Font_FT : public Font {
public:
	FT_Face face = nullptr; ///< font
	std::vector <Glyph> gs;
	int height; ///< height specified by user (rounded)
	
	Font_FT(bool &ok, const char* filename, float pt, uint32_t index);
	Font_FT(bool &ok, const void *mem, size_t mem_sz, float pt);
	~Font_FT();
	bool has_glyph(char32_t cp);
	bool load_glyph(char32_t cp);
	void unload_glyph(char32_t cp);
	void update_info(Info &info);
	void clear_glyphs();
	const Glyph* get_glyph(char32_t cp) const;
	std::vector <Glyph> get_glyphs() const;
};

static FT_Library lib_ft = nullptr; ///< library instance
static int lib_ft_cou = 0; ///< library reference count



Font_FT::Font_FT(bool &ok, const char* filename, float pt, uint32_t index) {
	VLOGI("Font::load() {}", filename);
	if (!load_ft_lib()) { // increase refcount
		VLOGE("Font::load() failed");
		return;
	}
	
	// try load font
	int error;
	if (( error = FT_New_Face(lib_ft, filename, index, &face) )) {
		VLOGE("Font::load() FT_New_Face: {}", FTERR(error));
		return;
	}
	VLOGD("  {} ({} glyphs total)", face->family_name, face->num_glyphs);
	
	// try set font height
	if (( error = FT_Set_Char_Size(face, 0, PX_TO_FT(pt), 0, 0) )) {
		VLOGE("Font::load() FT_Set_Char_Size: {}", FTERR(error));
		return;
	}
	
	height = round(pt);
	ok = true;
}
Font_FT::Font_FT(bool &ok, const void *mem, size_t mem_sz, float pt) {
	VLOGD("Font::load() from memory");
	if (!load_ft_lib()) { // increase refcount
		VLOGE("Font::load() failed");
		return;
	}
	
	// try load font
	int error;
	if (( error = FT_New_Memory_Face(lib_ft, static_cast<const FT_Byte*>(mem), mem_sz, 0, &face) )) {
		VLOGE("Font::load() FT_New_Face: {}", FTERR(error));
		return;
	}
	VLOGD("  {} ({} glyphs total)", face->family_name, face->num_glyphs);
	
	// try set font height
	if (( error = FT_Set_Char_Size(face, 0, PX_TO_FT(pt), 0, 0) )) {
		VLOGE("Font::load() FT_Set_Char_Size: {}", FTERR(error));
		return;
	}
	
	height = round(pt);
	ok = true;
}
Font_FT::~Font_FT() {
	if (face) FT_Done_Face(face);
	unload_ft_lib(); // decrease refcount
}
bool Font_FT::has_glyph(char32_t cp) {
	return FT_Get_Char_Index(face, cp) != 0;
}
bool Font_FT::load_glyph(char32_t cp) {
	// check if glyph already loaded
	if (get_glyph(cp)) return true;
	
	// get FT index
	int gi = FT_Get_Char_Index(face, cp);
	if (!gi) VLOGD("Font::load_glyph() doesn't exist: {:#x}", (uint32_t) cp);
	
	// try render (index zero corresponds to 'missing' glyph, which should be always available)
	if (int error = FT_Load_Glyph(face, gi, FT_LOAD_DEFAULT)) {
		VLOGE("Font::load_glyph() FT_Load_Glyph() for {:#x}: {}", (uint32_t) cp, FTERR(error));
		return false;
	}
	
	FT_Render_Mode ft_renmode;
	switch (renmode)
	{
		case RENMODE_ALPHA8: ft_renmode = FT_RENDER_MODE_NORMAL; break;
		case RENMODE_MONO:   ft_renmode = FT_RENDER_MODE_MONO;   break;
	}
	if (int error = FT_Render_Glyph(face->glyph, ft_renmode)) {
		VLOGE("Font::load_glyph() FT_Render_Glyph() for {:#x}: {}", (uint32_t) cp, FTERR(error));
		return false;
	}
	
	// get slot and image w & h
	FT_GlyphSlot& fg = face->glyph;
	int w = fg->bitmap.width;
	int h = fg->bitmap.rows;
	
	// add glyph
	gs.emplace_back();
	auto &g = gs.back();
	
	// set params
	g.cp = cp;
	g.off.x = fg->bitmap_left;
	g.off.y = height - fg->bitmap_top;
	g.xadv = FT_TO_PX(fg->advance.x);
	
	g.size = {w, h};
	if (!w || !h) return true; // yep, possible
	g.image.resize(w*h);
	
	// copy image
	switch (renmode)
	{
	case RENMODE_ALPHA8:
		for (int y=0; y<h; y++) {
			uint8_t *dst = &g.image[y*w];
			uint8_t *src = fg->bitmap.buffer + y * fg->bitmap.pitch;
			memcpy(dst, src, w);
		}
		break;
		
	case RENMODE_MONO:
		for (int y=0; y<h; y++) {
			uint8_t *dst = &g.image[y*w];
			uint8_t *src = fg->bitmap.buffer + y * fg->bitmap.pitch;
			for (int x=0; x<w; x++) {
				dst[x] = src[x/8] & (0x80 >> (x%8)) ? 255 : 0;
			}
		}
		break;
	}
	
	return true;
}
void Font_FT::unload_glyph(char32_t cp) {
	auto g = get_glyph(cp);
	if (g) gs.erase( gs.begin() + (g - gs.data()) ); // pointer distance, yep
}
void Font_FT::update_info(Info &info) {
	info.line = FT_TO_PX(face->size->metrics.height);
	info.line = std::max(info.line, height);
	
	if (gs.empty()) info.width = 0;
	else {
		// check if width monowide
		info.width = gs[0].size.x;
		for (auto &g : gs) if (info.width != g.size.x) {info.width = 0; break;}
	}
	
	// no kerning for monowide
	if (info.width) info.kerning = false;
	
	// try to get kerning values
	if (info.kerning) {
		if (!FT_HAS_KERNING(face)) VLOGE("Font::update_info() no kerning");
		else {
			// check all glyph pairs
			for (auto &g : gs) {
				for (auto &n : gs) {
					FT_Vector ker;
					FT_Get_Kerning(face, g.cp, n.cp, FT_KERNING_DEFAULT, &ker);
					if (ker.x || ker.y) { // can return no error but zero vector
						KernPair kp;
						kp.cp = n.cp;
						kp.off.x = -FT_TO_PX(ker.x); // not sure why negative
						kp.off.y = FT_TO_PX(ker.y);
						g.kerns.push_back(kp);
					}
				}
			}
		}
	}
	
	// set mode if needed
	if (info.width) info.mode = info.width;
	else if (info.mode) // calculate mode
	{
		struct Modv {
			int width;
			int count;
		};
		
		// gather all width values with count how many glyphs has them
		std::vector <Modv> ws;
		for (auto &g : gs) {
			auto it = std::find_if( ws.begin(), ws.end(), [&g](auto &&w) {return w.width == g.xadv;} );
			if (it != ws.end()) ++it->count; // encountered - increase
			else ws.push_back({g.xadv, 1}); // new width value
		}
		
		// find most used width value
		Modv mod {};
		for (auto &w : ws) {
			if (mod.count < w.count)
				mod = w;
		}
		info.mode = mod.width;
	}
}
void Font_FT::clear_glyphs() {
	gs.clear();
}
const Font::Glyph *Font_FT::get_glyph(char32_t cp) const {
	for (const auto &g : gs) if (g.cp == cp) return &g;
	return nullptr;
}
std::vector <Font::Glyph> Font_FT::get_glyphs() const {
	return gs;
}



bool Font::load_ft_lib() {
	// increase refcount
	++lib_ft_cou;
	if (lib_ft_cou > 1) return true;
	
	VLOGD("Font::load_ft_lib() compiled version: {}.{}.{}", FREETYPE_MAJOR, FREETYPE_MINOR, FREETYPE_PATCH);
	
	// zero refcount - try to init
	if (int error = FT_Init_FreeType(&lib_ft)) {
		VLOGE("Font::load_ft_lib() FT_Init_FreeType: {}", FTERR(error));
		lib_ft_cou = 0;
		return false;
	}
	
	// print version
	int maj, min, pat;
	FT_Library_Version(lib_ft, &maj, &min, &pat);
	VLOGD("Font::load_ft_lib() linked version: {}.{}.{}", maj, min, pat);

	return true;
}
bool Font::unload_ft_lib() {
	// decrease refcount
	if (!lib_ft_cou) return true;
	if (--lib_ft_cou > 0) return true;
	
	// refcount is zero, but wasn't - try to deinit
	if (int error = FT_Done_FreeType(lib_ft)) {
		VLOGE("Font::unload_ft_lib() FT_Done_FreeType: {}", FTERR(error));
		return false;
	}
	
	return true;
}
Font* Font::load_ft (const char* filename, float pt, uint32_t index) {
	bool ok = false;
	auto p = new Font_FT (ok, filename, pt, index);
	if (!ok) {
		delete p;
		return nullptr;
	}
	return p;
}
Font* Font::load_ft(const void *mem, size_t size, float pt) {
	bool ok = false;
	auto p = new Font_FT (ok, mem, size, pt);
	if (!ok) {
		delete p;
		return nullptr;
	}
	return p;
}
#endif
