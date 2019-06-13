#ifndef REN_IMM_HPP
#define REN_IMM_HPP

#include <cinttypes>
#include <functional>
#include "vaslib/vas_math.hpp"
#include "texture.hpp"

class  Camera;
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
	enum DefCtxIx
	{
		DEFCTX_NONE,  ///< Doesn't draw anything
		DEFCTX_WORLD, ///< Draws with RenderControl world camera to default framebuffer with imm* shaders
		DEFCTX_UI     ///< Same as DEFCTX_WORLD, but using RenderControl UI camera
	};
	
	/// Effect command
	enum EffectCmd
	{
		EFF_POP, ///< Disables last applied effect
		EFF_ADDITIVE_BLEND ///< Enables additive blending
	};
	
	///
	struct Context
	{
		Camera* cam     = nullptr; ///< Camera pointer; if null context isn't used
		Shader* sh      = nullptr; ///< Main shader; if null context isn't used
		Shader* sh_text = nullptr; ///< Text drawing shader, may be null
		
		/// Callback called before rendering with context, may change values in it. 
		/// If returns false, context won't be rendered
		std::function<bool(Context&)> cb_check;
		
		std::function<void()> cb_pp_begin; ///< Called before rendering with context (after check)
		std::function<void()> cb_pp_end; ///< Called after rendering with context finished
	};
	
	size_t dbg_buffer_size = 0; ///< Shows current size of internal buffer, in bytes
	size_t dbg_buffer_usage = 0; ///< Shows how much of buffer used last frame, in bytes
	
	static RenImm& get(); ///< Returns singleton
	
	
	
	/// Returns ID
	virtual size_t add_context (Context ctx) = 0;
	
	/// Returns context or null if it doesn't exist
	virtual Context* get_context (size_t id) = 0;
	
	/// Sets drawing mode by ID. If ID is zero, doesn't render anything.
	virtual void set_context (size_t id) = 0;
	
	
	
	/// Draw filled rectangle
	virtual void draw_rect (const Rectfp& dst, uint32_t clr) = 0;
	
	/// Draw frame
	virtual void draw_frame (const Rectfp& dst, uint32_t clr, int frame_width = 1) = 0;
	
	/// Draw image
	virtual void draw_image (const Rectfp& dst, const TextureReg& tex, uint32_t clr = (unsigned) -1) = 0;
	
	/// Draw image
	virtual void draw_image (uint tex, const Rectfp& dst, const Rectfp& src, uint32_t clr = (unsigned) -1) = 0;

	
	
	/// Draw filled rectangle
	virtual void draw_rect_rot (const Rectfp& dst, uint32_t clr, float rot) = 0;
	
	/// Draw frame
	virtual void draw_frame_rot (const Rectfp& dst, uint32_t clr, float rot, int frame_width = 1) = 0;
	
	/// Draw image; rotation is in radians
	virtual void draw_image_rot (const Rectfp& dst, const TextureReg& tex, float rot, uint32_t clr = (unsigned) -1) = 0;
	
	/// Draw image; rotation is in radians
	virtual void draw_image_rot (uint tex, const Rectfp& dst, const Rectfp& src, float rot, uint32_t clr = (unsigned) -1) = 0;
	
	
	
	/// Draw crude line
	virtual void draw_line (vec2fp p0, vec2fp p1, uint32_t clr, int width = 2) = 0;
	
	/// Draw outlined circle with lines
	virtual void draw_radius (vec2fp pos, float radius, uint32_t clr, int width = 2) = 0;
	
	/// Draw filled circle with triangles
	virtual void draw_circle (vec2fp pos, float radius, uint32_t clr) = 0;
	
	/// Draw string starting at specified coordinates (may sort glyphs)
	virtual void draw_text (vec2fp at, TextRenderInfo& tri, uint32_t clr, bool centered = false, float size_k = 1.f) = 0;
	
	
	
	/// Draw non-null ASCII string starting at specified coordinates
	virtual void draw_text (vec2fp at, std::string_view str, uint32_t clr, bool centered = false, float size_k = 1.f) = 0;
	
	/// Draw non-null unicode string starting at specified coordinates
	virtual void draw_text (vec2fp at, std::u32string_view str, uint32_t clr, bool centered = false, float size_k = 1.f) = 0;
	
	/// Returns size of non-null ASCII string
	static vec2i text_size (std::string_view str);
	
	
	
	/// Clips viewport at intersection of this and previous clip rect
	virtual void clip_push( Rect r ) = 0;
	
	/// Clips viewport at previous clip rect
	virtual void clip_pop() = 0;
	
	/// Executes command
	virtual void draw_cmd( EffectCmd cmd ) = 0;
	
	
protected:
	friend class RenderControl_Impl;
	static bool init(); ///< Initializes singleton
	virtual ~RenImm() = default;
	virtual void render() = 0;
};

#endif // REN_IMM_HPP
