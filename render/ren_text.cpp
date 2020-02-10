#include <unordered_map>
#include "core/settings.hpp"
#include "vaslib/vas_log.hpp"
#include "vaslib/vas_atlas_packer.hpp"
#include "vaslib/vas_font.hpp"
#include "control.hpp"
#include "ren_text.hpp"
#include "texture.hpp"

static std::vector<std::vector<char32_t>> tui_char_get_alts() {return {};}

#define DEBUG_ATLAS 0 // terminates after building atlas

#if DEBUG_ATLAS
#include "utils/res_image.hpp"
#endif



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
		vec2fp size; ///< pixels
		
		// composition params
		vec2fp off; ///< Drawing offset, pixels
		float xadv; ///< Horizontal advance
	};
	
	struct FontData
	{
		std::unordered_map <char32_t, Glyph> glyphs;
		std::vector <std::unique_ptr <Texture>> texs; ///< atlases: 2D, bilinear, single-channel (alpha as R)
		Glyph miss; ///< 'missing glyph' info
		Glyph white; ///< white rectangle
		
		bool is_mono_flag;
		float w_mode;
		float line_ht;
	};
	
	std::vector <std::shared_ptr<FontData>> fonts;
	std::unordered_map <char32_t, std::vector<char32_t>> gs_alts;
	
	
	
	RenText_Impl()
	{
		TimeSpan is_t0 = TimeSpan::since_start();
		
		auto& sets = AppSettings::get();
		fonts.resize(3);
		
		VLOGD("Loading default (primary) font");
		fonts[0] = load_font( sets.font_path.c_str(), sets.font_pt );
		if (!fonts[0]) {
			VLOGE("Can't load primary font");
			throw std::runtime_error("see log for details");
		}
		
		if (!fonts[0]->is_mono_flag) {
			VLOGE("Only monowide font can be used as primary");
			throw std::runtime_error("see log for details");
		}
		
		if ((sets.font_path == sets.font_dbg_path && aequ(sets.font_pt, sets.font_dbg_pt, 0.5f)) || sets.font_dbg_path.empty()) {
			VLOGW("Using primary font for debug");
			fonts[1] = fonts[0];
		}
		else {
			VLOGD("Loading debug font");
			fonts[1] = load_font( sets.font_dbg_path.c_str(), sets.font_dbg_pt );
			if (!fonts[1]) {
				VLOGW("Can't load debug font, using primary");
				fonts[1] = fonts[0];
			}
		}
		
		VLOGI("Fonts loaded in {:.3f} seconds", (TimeSpan::since_start() - is_t0).seconds());
	}
	void build (TextRenderInfo& b) override
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
		
		vec2fp pos = {}, max = {};
		vec2fp a_size = {};
		int tab_count = 0; // n of chars in current line
		
		if (!b.info_only)
			b.cs.reserve( b.cs.size() + len );
		
		auto& ft = gf(b.font);
		
		auto newline = [&]()
		{
			a_size.x = std::max( a_size.x, pos.x );
			pos.x = 0;
			pos.y += ft.line_ht;
			tab_count = 0;
		};
		
		const int n_tab = b.tab_width ? *b.tab_width : tab_width;
		
		for (int i = 0; i < len; ++i)
		{
			char32_t ch = b.str_a ? b.str_a[i] : b.str[i];
			
			if (ch == '\n')
			{
				newline();
				continue;
			}
			if (ch == '\t')
			{
				float w = ft.glyphs.at(' ').xadv * (n_tab - tab_count % n_tab);
				if (pos.x + w > b.max_width) newline();
				else {
					pos.x += w;
					max.x = std::max(max.x, pos.x);
					tab_count = 0;
				}
				continue;
			}
			a_size.y = pos.y + ft.line_ht;
			
			auto it = ft.glyphs.find( ch );
			auto& g = (it != ft.glyphs.end()) ? it->second : ft.miss;
			
			vec2fp p1 = pos + g.off + g.size;
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
			++tab_count;
		}
		
		a_size.x = std::max( a_size.x, pos.x );
		b.size = b.strict_size ? max : a_size;
	}
	vec2i predict_size( vec2i str_size, FontIndex i ) override
	{
		return (mxc_size(i) * str_size).int_ceil();
	}
	TextureReg get_white_rect(FontIndex font) override
	{
		return gf(font).white.tex;
	}
	bool   is_mono    ( FontIndex i ) override {return gf(i).is_mono_flag;}
	float  width_mode ( FontIndex i ) override {return gf(i).w_mode;}
	float  line_height( FontIndex i ) override {return gf(i).line_ht;}
	vec2fp mxc_size   ( FontIndex i ) override {return {gf(i).w_mode, gf(i).line_ht};}
	
	TextRenderInfo::GlyphInfo get_glyph(char32_t cp, FontIndex font) override
	{
		auto& ft = gf(font);
		auto it = ft.glyphs.find(cp);
		auto& g = (it != ft.glyphs.end()) ? it->second : ft.miss;
		return {{g.off, g.size, true}, g.tex};
	}
	
	
	
	FontData& gf(FontIndex i)
	{
		return *fonts[static_cast<size_t>(i)];
	}
	static std::shared_ptr<FontData> load_font(const char *fname, float pt)
	{
		const float ss_k = AppSettings::get().font_supersample;
		
		VLOGV("RenText::load_font() called: {}px (scaled to {}px) \"{}\"", pt, pt * ss_k, fname);
		std::unique_ptr <vas::Font> font (vas::Font::load_auto (fname, pt * ss_k));
		if (!font) {
			VLOGE("Can't load font");
			return {};
		}
		
		font->load_glyph((char32_t) -1); // for missing glyph symbol
		font->load_glyphs(32, 126);
		
		size_t alt_c = 0;
		std::vector<std::vector<char32_t>> alts = tui_char_get_alts();
		std::vector<std::pair<char32_t, char32_t>> alt_add;
		alt_add.reserve(alts.size());
		
		for (auto& ar : alts)
		{
			if (ar.empty()) continue;
			size_t i = 0;
			for (; i < ar.size(); ++i) if (font->has_glyph(ar[i])) break;
			if (i == ar.size()) VLOGW("No alt characters for {:#x}", (uint32_t) ar.front());
			else {
				if (!i) ++alt_c;
				else {
					VLOGV("  Using alt character: {} of {} ({:#x}) for {:#x}", i, ar.size() - 1, (uint32_t) ar[i], (uint32_t) ar.front());
					alt_add.emplace_back(ar.front(), ar[i]);
				}
				font->load_glyph(ar[i]);
			}
		}
		VLOGV("  Non-alt characters: {} of {}", alt_c, alts.size());
		
		vas::Font::Info inf;
		inf.kerning = false;
		inf.mode = 1;
		font->update_info(inf);
		
		auto fd = std::make_shared<FontData>();
		fd->is_mono_flag = inf.width != 0;
		fd->w_mode  = inf.mode /ss_k;
		fd->line_ht = inf.line /ss_k;
		
		
		// create packed alpha atlas
		
		AtlasBuilder abd;
		abd.pk.reset( new AtlasPacker );
		
		abd.pk->bpp = 1;
		abd.pk->min_size = 4;
		abd.pk->max_size = std::max( RenderControl::get().get_max_tex(), 32768 );
		abd.pk->space_size = 1;
		
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
	VLOGD("Atlas built in {:.3f} seconds", is_t1.seconds());
		
		
		// generate textures
		VLOGV("  generated {} atlases", is.size());
		fd->texs.resize( is.size() );
		for (size_t i = 0; i < is.size(); ++i)
		{
			auto& img = is[i];
			VLOGV("  {}: {}x{}, sprites: {}", i, img.info.w, img.info.h, img.info.sprs.size());
			
			fd->texs[i].reset( Texture::create_from({ img.info.w, img.info.h }, Texture::FMT_SINGLE,
			                                        img.px.data(), Texture::FIL_LINEAR ) );
#if DEBUG_ATLAS
			static int fi = 0; ++fi;
			if (!i && fi == DEBUG_ATLAS+1)
			{
				ImageInfo ii;
				ii.reset({ img.info.w, img.info.h }, ImageInfo::FMT_ALPHA);
				memcpy( ii.raw(), img.px.data(), img.px.size() );
				ii.save("test.png");
				exit(1);
			}
#endif
		}
		
		
		// add symbols
		fd->glyphs.reserve( gs.size() + 1 + alt_add.size() );
		
		VLOGV("  glyphs count: {}", gs.size());
		
		for (auto &fg : gs)
		{
			auto spr = abd.pk->get(fg.cp);
			if (!spr) continue;
			Glyph rg;
			
			auto tex = fd->texs[ spr->index ].get();
			rg.tex = { tex, tex->to_texcoord({ {spr->x, spr->y}, {spr->w, spr->h}, true }) };
			rg.size = vec2fp(spr->w, spr->h) / ss_k;
			
			rg.off = vec2fp(fg.off) / ss_k;
			rg.xadv = fg.xadv /ss_k;
//			if (fd->is_mono_flag) rg.xadv = w_mode;
			
			if (fg.cp == (char32_t) -1) fd->miss = rg;
			else fd->glyphs.emplace(fg.cp, rg);
		}
		
		for (auto& p : alt_add) {
			auto it = fd->glyphs.find(p.second);
			fd->glyphs.emplace(p.first, it->second);
		}
		
		fd->is_mono_flag = font->monowide_check( fd->w_mode / 6, 8 );
		
		// add white rect
		if (auto spr = abd.pk->get(0)) {
			Glyph& rg = fd->white;
			
			auto tex = fd->texs[ spr->index ].get();
			rg.tex = { tex, tex->to_texcoord({ {spr->x, spr->y}, {spr->w, spr->h}, true }) };
			rg.size = vec2fp(spr->w, spr->h);
			
			rg.off = {};
			rg.xadv = fd->w_mode;
			
			vec2fp c = rg.tex.tc.center();
			rg.tex.tc.lower(c);
			rg.tex.tc.upper(c);
		}
		
		// make all characters monowide
		if (fd->is_mono_flag)
		{
			for (auto& g : fd->glyphs) g.second.xadv = fd->w_mode;
			fd->miss.xadv = fd->w_mode;
		}
		
		// generate space glyph if needed
		auto wsp = fd->glyphs.find(' ');
		if (wsp == fd->glyphs.end() || wsp->second.xadv < 1)
		{
		    Glyph g = fd->white;
			g.xadv = fd->w_mode;
			fd->glyphs[' '] = g;
		}
		
		VLOGV("RenText::load_font() OK");
		return fd;
	}
};



static RenText_Impl* rni;
RenText& RenText::get() {
	if (!rni) LOG_THROW_X("RenText::get() null");
	return *rni;
}
RenText* RenText::init() {return rni = new RenText_Impl;}
RenText::~RenText() {rni = nullptr;}
