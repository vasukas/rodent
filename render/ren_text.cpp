#include <unordered_map>
#include "../settings.hpp"
#include "vaslib/vas_log.hpp"
#include "vaslib/vas_atlas_packer.hpp"
#include "vaslib/vas_font.hpp"
#include "control.hpp"
#include "ren_text.hpp"
#include "texture.hpp"

// #include "core/res_image.hpp"



void TextRenderInfo::build()
{
	RenText::get().build(*this);
}



class RenText_Impl : public RenText
{
public:
	/// Glyph info
	struct Glyph
	{
		// rendering params
		TextureReg tex;
		vec2i size; ///< pixels
		
		// composition params
		vec2i off; ///< Drawing offset, pixels
		int xadv; ///< Horizontal advance
	};
	
	struct FontData
	{
		std::unordered_map <char32_t, Glyph> glyphs;
		std::vector <std::unique_ptr <Texture>> texs; ///< atlases: 2D, bilinear, single-channel (alpha as R)
		Glyph miss; ///< 'missing glyph' info
		Glyph white; ///< white rectangle
		
		bool is_mono_flag;
		uint w_mode;
		uint line_ht;
	};
	
	std::vector <std::shared_ptr<FontData>> fonts;
	std::unordered_map <char32_t, std::vector<char32_t>> gs_alts;
	
	
	
	RenText_Impl(bool& ok)
	{
		TimeSpan is_t0 = TimeSpan::since_start();
		
		auto& sets = AppSettings::get();
		fonts.resize(3);
		
		fonts[0] = load_font( sets.font_path.c_str(), sets.font_pt );
		if (!fonts[0]) {
			VLOGE("Can't load primary font");
			return;
		}
		
		if (sets.font_path == sets.font_ui_path && sets.font_pt == sets.font_ui_pt) {
			VLOGW("Using primary font for UI");
			fonts[1] = fonts[0];
		}
		else {
			fonts[1] = load_font( sets.font_ui_path.c_str(), sets.font_ui_pt );
			if (!fonts[1]) {
				VLOGW("Can't load UI font, using primary");
				fonts[1] = fonts[0];
			}
		}
		
		if (sets.font_path == sets.font_dbg_path && sets.font_pt == sets.font_dbg_pt) {
			VLOGW("Using primary font for debug");
			fonts[2] = fonts[0];
		}
		else if (sets.font_ui_path == sets.font_dbg_path && sets.font_ui_pt == sets.font_dbg_pt) {
			VLOGW("Using UI font for debug");
			fonts[2] = fonts[1];
		}
		else {
			fonts[2] = load_font( sets.font_dbg_path.c_str(), sets.font_dbg_pt );
			if (!fonts[2]) {
				VLOGW("Can't load debug font, using UI");
				fonts[2] = fonts[1];
			}
		}
		
		if (!fonts[1]->is_mono_flag) {
			if (fonts[2]->is_mono_flag) {
				VLOGW("Can't load UI font, using debug");
				fonts[1] = fonts[2];
			}
			else {
				VLOGE("Only monowide font can be used for UI");
				return;
			}
		}
		
		VLOGI("Fonts loaded in {:6.3} seconds", (TimeSpan::since_start() - is_t0).seconds());
		ok = true;
	}
	void build (TextRenderInfo& b)
	{
		b.size = {};
		if (b.cs_clear) b.cs.clear();
		if (!b.str && !b.str_a) return;
		
		int len;
		if (b.length >= 0) len = b.length;
		else
		{
			if (!b.str_a) for (len = 0; b.str  [len]; ++len) ;
			else          for (len = 0; b.str_a[len]; ++len) ;
		}
		
		vec2i pos = {}, max = {};
		vec2i a_size = {};
		
		if (!b.info_only)
			b.cs.reserve( b.cs.size() + len );
		
		auto& ft = gf(b.font);
		
		auto newline = [&]()
		{
			a_size.x = std::max( a_size.x, pos.x );
			pos.x = 0;
			pos.y += ft.line_ht;
		};
		
		for (int i = 0; i < len; ++i)
		{
			char32_t ch = b.str_a ? b.str_a[i] : b.str[i];
			
			if (ch == '\n')
			{
				newline();
				continue;
			}
			a_size.y = pos.y + ft.line_ht;
			
			auto it = ft.glyphs.find( ch );
			auto& g = (it != ft.glyphs.end()) ? it->second : ft.miss;
			
			vec2i p1 = pos + g.off + g.size;
			if (std::max(p1.x, pos.x + g.xadv) > b.max_width)
			{
				newline();
				--i;
				continue;
			}
			
			if (!b.info_only)
			{
				auto& c = b.cs.emplace_back();
				c.pos = {pos + g.off, g.size, true};
				c.tex = g.tex;
			}
			
			max = ::max(max, p1);
			pos.x += g.xadv;
		}
		
		a_size.x = std::max( a_size.x, pos.x );
		b.size = b.strict_size ? max : a_size;
	}
	vec2i predict_size( vec2i str_size, FontIndex i )
	{
		return str_size * vec2i( gf(i).w_mode, gf(i).line_ht );
	}
	TextureReg get_white_rect()
	{
		return gf(FontIndex::Default).white.tex;
	}
	bool is_mono   ( FontIndex i ) {return gf(i).is_mono_flag;}
	int width_mode ( FontIndex i ) {return gf(i).w_mode;}
	int line_height( FontIndex i ) {return gf(i).line_ht;}
	vec2i mxc_size ( FontIndex i ) {return vec2i(gf(i).w_mode, gf(i).line_ht);}
	
	void add_alts(std::vector<std::vector<char32_t>> alts)
	{
		for (auto& f : fonts)
		for (auto& ar : alts)
		{
			if (ar.size() < 2) continue;
			
			size_t i = 0;
			for (; i < ar.size(); ++i)
			{
				auto it = f->glyphs.find( ar[i] );
				if (it != f->glyphs.end())
				{
					if (i) f->glyphs.emplace( ar.front(), it->second );
					break;
				}
			}
			
			if (i == ar.size())
				VLOGW("RenText::add_alts() failed for {:#x}", (uint32_t) ar.front());
		}
	}
	
	
	
	FontData& gf(FontIndex i)
	{
		return *fonts[static_cast<size_t>(i)];
	}
	std::shared_ptr<FontData> load_font(const char *fname, float pt)
	{
		VLOGV("RenText::load_font() called: {}pt \"{}\"", pt, fname);
		std::unique_ptr <vas::Font> font (vas::Font::load_ft (fname, pt));
		if (!font)
		{
			VLOGE("Can't load font");
			return {};
		}
		
		font->load_glyph((char32_t) -1); // for missing glyph symbol
		font->load_glyphs(32, 126);
		
		vas::Font::Info inf;
		inf.kerning = false;
		inf.mode = 1;
		font->update_info(inf);
		
		auto fd = std::make_shared<FontData>();
		fd->is_mono_flag = inf.width != 0;
		fd->w_mode = inf.mode;
		fd->line_ht = inf.line;
		
		
		// create packed alpha atlas
		
		AtlasBuilder abd;
		abd.pk.reset( new AtlasPacker );
		
		abd.pk->bpp = 1;
		abd.pk->min_size = 1;
		abd.pk->max_size = RenderControl::get().get_max_tex();
		abd.pk->space_size = 4;
		
		// white pixel image (used for rectangles)
		uint8_t wpx[9];
		for (int i=0; i<9; ++i) wpx[i] = 255;
		abd.add_static( {0, 3, 3}, wpx );
		
		// add glyphs
		auto gs = font->get_glyphs();
		for (auto &g : gs)
			abd.add_static( {g.cp, g.size.x, g.size.y}, g.image.data() );
		
		// build atlas
	TimeSpan is_t0 = TimeSpan::since_start();
		auto is = abd.build();
	TimeSpan is_t1 = TimeSpan::since_start() - is_t0;
	VLOGD("Atlas built in {:6.3} seconds", is_t1.seconds());
		
		
		// generate textures
		VLOGV("  generated {} atlases", is.size());
		fd->texs.resize( is.size() );
		for (size_t i = 0; i < is.size(); ++i)
		{
			auto& img = is[i];
			VLOGV("  {}: {}x{}, sprites: {}", i, img.info.w, img.info.h, img.info.sprs.size());
			
			fd->texs[i].reset( Texture::create_from({ img.info.w, img.info.h }, Texture::FMT_SINGLE,
			                                        img.px.data(), Texture::FIL_NEAREST ) );
			
//			if (!i)
//			{
//				ImageInfo ii;
//				ii.reset({ img.info.w, img.info.h }, ImageInfo::FMT_ALPHA);
//				memcpy( ii.raw(), img.px.data(), img.px.size() );
//				ii.save("test.png");
//				return;
//			}
		}
		
		
		// add symbols
		fd->glyphs.reserve( gs.size() + 1 );
		
		VLOGV("  glyphs count: {}", gs.size());
		
		for (auto &fg : gs)
		{
			auto spr = abd.pk->get(fg.cp);
			if (!spr) continue;
			Glyph rg;
			
			auto tex = fd->texs[ spr->index ].get();
			rg.tex = { tex, tex->to_texcoord({ {spr->x, spr->y}, {spr->w, spr->h}, true }) };
			rg.size = {spr->w, spr->h};
			
			rg.off = fg.off;
			rg.xadv = fg.xadv;
//			if (fd->is_mono_flag) rg.xadv = w_mode;
			
			if (fg.cp == (char32_t) -1) fd->miss = rg;
			else fd->glyphs.emplace(fg.cp, rg);
		}
		
		fd->is_mono_flag = font->monowide_check( fd->w_mode / 6, 8 );
		
		// add white rect
		if (auto spr = abd.pk->get(0)) {
			Glyph& rg = fd->white;
			
			auto tex = fd->texs[ spr->index ].get();
			rg.tex = { tex, tex->to_texcoord({ {spr->x, spr->y}, {spr->w, spr->h}, true }) };
			rg.size = {spr->w, spr->h};
			
			rg.off = {};
			rg.xadv = fd->w_mode;
		}
		
		VLOGV("RenText::load_font() OK");
		return fd;
	}
};



static RenText_Impl* rti;
bool RenText::init()
{
	if (!rti)
	{
		bool ok = false;
		rti = new RenText_Impl (ok);
		if (!ok)
		{
			VLOGE("RenText::init() failed");
			delete rti;
			return false;
		}
		VLOGI("RenText::init() ok");
	}
	return true;
}
RenText& RenText::get()
{
	if (!rti) LOG_THROW_X("RenText::get() null");
	return *rti;
}
RenText::~RenText() {
	rti = nullptr;
}
