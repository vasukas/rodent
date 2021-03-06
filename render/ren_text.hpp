#ifndef REN_TEXT_HPP
#define REN_TEXT_HPP

#include <vector>
#include "vaslib/vas_math.hpp"
#include "texture.hpp"



enum class FontIndex
{
	Mono,
	Debug
};



struct TextRenderInfo
{
	// input
	
	const char32_t* str = nullptr;
	const char* str_a = nullptr; ///< ASCII string (NOT UTF-8!!!). Preferred over 'str'
	int length = -1; ///< Char count or -1 if null-terminated
	
	FontIndex font = FontIndex::Mono;
	bool info_only = false; ///< Don't build rendering info, only size
	bool strict_size = false; ///< Return exact size without any spacing
	
	bool cs_clear = true; ///< If set to true, cs cleared on each build. Otherwise it's extended
	int max_width = std::numeric_limits<int>::max(); ///< Maximum rendering width
	
	std::optional<int> tab_width = {}; ///< Use this instead of RenText::tab_width
	
	// output
	
	struct GlyphInfo
	{
		Rectfp pos; ///< Draw position (starting from zero)
		TextureReg tex; ///< Always valid
	};
	
	vec2fp size = {}; ///< Output dimensions, in pixels
	std::vector <GlyphInfo> cs; ///< Each represents single character from input string
	
	// funcs
	
	void build(); ///< Updates output info from input (calls RenText::build)
};



class RenText
{
public:
	int tab_width = 4; ///< Tabs are replaced by spaces
	
	static RenText& get(); ///< Returns singleton
	
	virtual void build( TextRenderInfo& info ) = 0;
	virtual vec2i predict_size( vec2i str_size, FontIndex font ) = 0; ///< Most probable size of such string in pixels
	
	virtual TextureReg get_white_rect(FontIndex font = FontIndex::Mono) = 0; ///< Returns info for 1x1 white rectangle
	
	virtual bool   is_mono    (FontIndex font) = 0; ///< Is font (roughly) monowide
	virtual float  width_mode (FontIndex font) = 0; ///< Average char width
	virtual float  line_height(FontIndex font) = 0; ///< Not character height
	virtual vec2fp mxc_size   (FontIndex font) = 0; ///< width_mode + line_height
	
	/// Returns glyph info as if it would be rendered at {0,0}
	virtual TextRenderInfo::GlyphInfo get_glyph(char32_t cp, FontIndex font) = 0;
	
protected:
	friend class RenderControl_Impl;
	static RenText* init();
	virtual ~RenText();
};

#endif // REN_TEXT_HPP
