#ifndef REN_IMM_HPP
#define REN_IMM_HPP

#include <memory>
#include <functional>
#include "vaslib/vas_math.hpp"
#include "texture.hpp"

enum class FontIndex;
class  Camera;
struct FColor;
class  Shader;
struct TextRenderInfo;



/// Immediate-mode batching renderer
class RenImm
{
public:
	/*
	   NOTES
	   
		In all functions with rotation position is center.
		In functions without rotation position is top-left.
	*/
	
	/// Default context IDs
	enum CtxIndex
	{
		DEFCTX_WORLD, ///< Draws with RenderControl world camera to default framebuffer with imm* shaders
//		DEFCTX_BACK,  ///< Same as DEFCTX_WORLD, but drawn before it
		DEFCTX_UI,    ///< Same as DEFCTX_WORLD, but using RenderControl UI camera
		
		DEFCTX_NONE   ///< Doesn't draw anything
	};
	
	/// Effect command
	enum EffectCmd
	{
		EFF_POP ///< Disables last applied effect
	};
	
	///
	struct Context
	{
		Camera* cam = nullptr; ///< Camera pointer; if null context isn't used
		Shader* sh  = nullptr; ///< Main shader; if null context isn't used
		Shader* sh_text = nullptr; ///< Required for draw_text(), may be null
	};
	
	static const uint32_t White = static_cast<uint32_t>(-1);
	
	static RenImm& get(); ///< Returns singleton
	
	
	
	/// Sets drawing context
	virtual void set_context (CtxIndex id) = 0;
	
	/// Throws if called with DEFCTX_NONE
	virtual Context& get_context (CtxIndex id) = 0;
	
	
	
	/// Draw filled rectangle
	virtual void draw_rect (const Rectfp& dst, uint32_t clr) = 0;
	
	/// Draw frame
	virtual void draw_frame (const Rectfp& dst, uint32_t clr, float frame_width = 1) = 0;
	
	/// Draw image
	virtual void draw_image (const Rectfp& dst, const TextureReg& tex, uint32_t clr = White) = 0;
	
	/// Draw image
	virtual void draw_image (uint tex, const Rectfp& dst, const Rectfp& src, uint32_t clr = White) = 0;

	
	
	/// Draw filled rectangle
	virtual void draw_rect_rot (const Rectfp& dst, uint32_t clr, float rot) = 0;
	
	/// Draw frame
	virtual void draw_frame_rot (const Rectfp& dst, uint32_t clr, float rot, float frame_width = 1) = 0;
	
	/// Draw image; rotation is in radians
	virtual void draw_image_rot (const Rectfp& dst, const TextureReg& tex, float rot, uint32_t clr = White) = 0;
	
	/// Draw image; rotation is in radians
	virtual void draw_image_rot (uint tex, const Rectfp& dst, const Rectfp& src, float rot, uint32_t clr = White) = 0;
	
	
	
	/// Draw crude line
	virtual void draw_line (vec2fp p0, vec2fp p1, uint32_t clr, float width = 2) = 0;
	
	/// Draw outlined circle with lines
	virtual void draw_radius (vec2fp pos, float radius, uint32_t clr, float width = 2, int segn = -1) = 0;
	
	/// Draw filled circle with triangles
	virtual void draw_circle (vec2fp pos, float radius, uint32_t clr, int segn = -1) = 0;
	
	/// Draw string starting at specified coordinates (may sort glyphs)
	virtual void draw_text (vec2fp at, TextRenderInfo& tri, uint32_t clr, bool centered = false, float size_k = 1.f) = 0;
	
	
	
	/// Draw ASCII string at specified coordinates
	virtual void draw_text (vec2fp at, std::string_view str, uint32_t clr = White, bool centered = false, float size_k = 1.f, FontIndex font = static_cast<FontIndex>(0)) = 0;
	
	/// Draw ASCII string, with separately colored characters (count, color)
	virtual void draw_text (vec2fp at, std::vector<std::pair<std::string, FColor>> strs) = 0;
	
	/// Draw ASCII string at specified coordinates with filled semi-transparent background. 
	/// Negative coordinates are treated as if offset from screen size - text size
	virtual void draw_text_hud (vec2fp at, std::string_view str, uint32_t clr = White, bool centered = false, float size_k = 1.f) = 0;
	
	/// Returns size of non-null ASCII string
	static vec2i text_size (std::string_view str);
	
	
	
	/// Adds raw vertices with solid white texture
	virtual void draw_vertices(const std::vector<vec2fp>& vs) = 0;
	
	/// Finishes adding raw vertices
	virtual void draw_vertices_end(uint32_t clr) = 0;

	
	
	/// Clips viewport at intersection of this and previous clip rect
	virtual void clip_push( Rect r ) = 0;
	
	/// Clips viewport at previous clip rect
	virtual void clip_pop() = 0;
	
//	/// Executes command
//	virtual void draw_cmd( EffectCmd cmd ) = 0;
// Note: blending is set in main render loop
	
	
protected:
	friend class RenderControl_Impl;
	static RenImm* init();
	virtual ~RenImm();
	
	friend class Postproc_Impl;
	virtual void render_pre() = 0;
	virtual void render(CtxIndex id) = 0;
	virtual void render_post() = 0;
};

#endif // REN_IMM_HPP
