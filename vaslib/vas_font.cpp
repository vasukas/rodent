#include "vas_font.hpp"
#include "vas_log.hpp"
using namespace vas;



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
	VLOGD("Font::load() {}", filename);
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
	if (int error = FT_Load_Glyph(face, gi, FT_LOAD_RENDER)) {
		VLOGE("Font::load_glyph() FT_Load_Glyph() for {:#x}: {}", (uint32_t) cp, FTERR(error));
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
	for (int y=0; y<h; y++) {
		uint8_t *dst = &g.image[y*w];
		uint8_t *src = fg->bitmap.buffer + y * fg->bitmap.pitch;
		memcpy(dst, src, w);
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



vec2i Font::Glyph::get_kern (char32_t next) {
	for (auto &k : kerns) if (k.cp == next) return k.off;
	return {};
}
int Font::load_glyphs(char32_t cp_first, char32_t cp_last) {
	int n = 0;
	for (; cp_first <= cp_last; ++cp_first)
		if (has_glyph(cp_first) && load_glyph(cp_first)) ++n;
	return n;
}
void Font::unload_glyphs(char32_t cp_first, char32_t cp_last) {
	for (; cp_first <= cp_last; ++cp_first)
		unload_glyph(cp_first);
}
bool Font::monowide_check(int tolerance, int threshold) {
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



class Font_Vas : public Font {
public:
	std::vector<Glyph> gs, lds;
	vec2i cz;
	
	bool has_glyph(char32_t cp) {
		for (size_t i = 0; i < lds.size(); ++i) if (lds[i].cp == cp) return true;
		return false;
	}
	bool load_glyph(char32_t cp) {
		if (get_glyph(cp)) return true;
		for (size_t i = 0; i < lds.size(); ++i) if (lds[i].cp == cp) {
			gs.push_back( lds[i] );
			return true;
		}
		return false;
	}
	void unload_glyph(char32_t cp) {
		auto g = get_glyph(cp);
		if (g) gs.erase( gs.begin() + (g - gs.data()) ); // pointer distance, yep
	}
	void update_info(Info &info) {
		info.line = cz.y;
		info.mode = info.width = cz.x;
		info.kerning = false;
	}
	void clear_glyphs() {gs.clear();}
	const Glyph* get_glyph(char32_t cp) const {
		for (size_t i = 0; i < gs.size(); ++i) if (gs[i].cp == cp) return &gs[i];
		return nullptr;
	}
	std::vector<Glyph> get_glyphs() const {
		return gs;
	}
};



#include "vas_file.hpp"
static const char vas_font_head[] = {"vasfont\0"};

Font* Font::load_vas(File& f)
{
	char head[9];
	if (9 != f.read(head, 9) || memcmp(head, vas_font_head, 9)) {
		VLOGE("Font::load_vas() invalid header (or newer version)");
		return nullptr;
	}
	
	std::vector<Glyph> gs;
	gs.resize( f.r16L() );
	int x = f.r8();
	int y = f.r8();
	
	for (auto& g : gs)
	{
		g.cp = f.r16L();
		g.off.x = static_cast<int8_t>(f.r8());
		g.off.y = static_cast<int8_t>(f.r8());
		g.size.x = f.r8();
		g.size.y = f.r8();
		g.image.resize( g.size.area() );
		f.read( g.image.data(), g.size.area() );
	}
	
	if (f.error_flag) {
		VLOGE("Font::load_vas() file error");
		return nullptr;
	}
	
	Font_Vas* ft = new Font_Vas;
	ft->lds = std::move(gs);
	ft->cz = {x, y};
	return ft;
}
bool Font::save_vas(File& f)
{
	if (!monowide_check(0, 5)) {
		VLOGE("Font::save_vas() not monowide (not implemented)");
		return false;
	}
	
	Info inf = {};
	inf.mode = 1;
	inf.kerning = false;
	update_info(inf);
	
	auto gs = get_glyphs();
	
	f.write(vas_font_head, 9); // two zero bytes
	f.w16L( gs.size() );
	f.w8( inf.mode );
	f.w8( inf.line );
	
	for (auto& g : gs)
	{	
		f.w32L( g.cp );
		f.w8( g.off.x );
		f.w8( g.off.y );
		f.w8( g.size.x );
		f.w8( g.size.y );
		f.write( g.image.data(), g.size.area() );
	}
	
	if (f.error_flag) {
		VLOGE("Font::save_vas() file error");
		return false;
	}
	return true;
}
