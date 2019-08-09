#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_log.hpp"
#include "camera.hpp"
#include "control.hpp"
#include "ren_imm.hpp"
#include "ren_text.hpp"
#include "shader.hpp"
#include "texture.hpp"



static const size_t INVIX = (size_t) -1;

vec2i RenImm::text_size( std::string_view str )
{
	TextRenderInfo tri;
	tri.str_a = str.data();
	tri.length = str.length();
	tri.info_only = true;
	tri.build();
	return tri.size;
}



class RenImm_Impl : public RenImm
{
public:
	enum // shader type index
	{
		SHAD_INITIAL, // set after reset
		SHAD_MAIN,
		SHAD_TEXT
	};
	struct Obj // draw object
	{
		size_t off, count; // Vertices in buffer
	};
	struct Cmd // command
	{
		enum Type
		{
			T_OBJ, // draw object, index into 'objs'
			T_SHAD, // set shader and camera matrix, index is SHAD_*
			T_CLIP, // set clip rect, index into 'clips' or INVIX for default
			T_TEX, // set texture, index is object
			T_CLR, // set color, index is uint32_t
			T_ECMD // effect command, index is EffectCmd
		};
		Type type;
		size_t index;
	};
	struct ContextInternal
	{
		Context ctx;
		std::vector <Cmd> cmds; // command list
		std::vector <std::pair <Rect, size_t>> clip_stack; // clipping rect stack
		
		size_t last_os = INVIX; // last object index for this context (see add_obj())
		GLuint last_tex = 0; // last set texture
		uint32_t last_clr; // last set color
		size_t last_shad = SHAD_INITIAL; // last set shader
	};
	
	GLA_VertexArray vao; ///< Buffer
	std::vector <float> data; ///< Buffer data to send
	size_t objs_off = 0; ///< Last vertices offset (same as data.size() / 4)
	
	std::vector <ContextInternal> ctxs;
	size_t ctx_cur = 0; ///< Current context index
	bool ctx_ok = false; ///< Is current context valid
	
	std::vector <Obj> objs; ///< for T_OBJ
	std::vector <Rect> clips; ///< for T_CLIP
	
	GLuint white_tex; ///< White rectangle texture
	Rectfp white_tc; ///< White rectangle texcoord
	
	GLint vp[4]; // for restoring after clip rects
	
	
	
#define u0 tc.lower().x
#define v0 tc.lower().y
#define u1 tc.upper().x
#define v1 tc.upper().y
	inline void add_rect
	(
	        float x0, float y0, float x1, float y1,
	        const Rectfp& tc
	) {
		// first triangle (11 - 21 - 12)
		
		data.push_back( x0 ); data.push_back( y0 );
		data.push_back( u0 ); data.push_back( v0 );
		
		data.push_back( x1 ); data.push_back( y0 );
		data.push_back( u1 ); data.push_back( v0 );
		
		data.push_back( x0 ); data.push_back( y1 );
		data.push_back( u0 ); data.push_back( v1 );
		
		// second triangle (21 - 12 - 22)
		
		data.push_back( x1 ); data.push_back( y0 );
		data.push_back( u1 ); data.push_back( v0 );
		
		data.push_back( x0 ); data.push_back( y1 );
		data.push_back( u0 ); data.push_back( v1 );
		
		data.push_back( x1 ); data.push_back( y1 );
		data.push_back( u1 ); data.push_back( v1 );
	}
	inline void add_rect
	(
	        float x0, float y0, float x1, float y1,
	        float x2, float y2, float x3, float y3,
	        const Rectfp& tc
    ) {
		// first triangle (11 - 21 - 12)
		
		data.push_back( x0 ); data.push_back( y0 );
		data.push_back( u0 ); data.push_back( v0 );
		
		data.push_back( x1 ); data.push_back( y1 );
		data.push_back( u1 ); data.push_back( v0 );
		
		data.push_back( x2 ); data.push_back( y2 );
		data.push_back( u0 ); data.push_back( v1 );
		
		// second triangle (21 - 12 - 22)
		
		data.push_back( x1 ); data.push_back( y1 );
		data.push_back( u1 ); data.push_back( v0 );
		
		data.push_back( x2 ); data.push_back( y2 );
		data.push_back( u0 ); data.push_back( v1 );
		
		data.push_back( x3 ); data.push_back( y3 );
		data.push_back( u1 ); data.push_back( v1 );
	}
#undef u0
#undef v0
#undef u1
#undef v1
	
	bool can_add (bool is_text)
	{
		if (!ctx_ok) return false; // was set by set_context()
		return !is_text || ctxs[ ctx_cur ].ctx.sh_text;
	}
	void add_obj (GLuint tex, uint32_t clr, bool is_text)
	{
		// called only if can_add() returned true
		
		size_t cou = data.size() /4 - objs_off;
		auto& cx = ctxs[ctx_cur];
		size_t shad = is_text ? SHAD_TEXT : SHAD_MAIN; // which shader index draw with
		
		// check if can just extend previous object instead of creating another draw call
		if (cx.last_os != INVIX && cx.last_tex == tex && cx.last_clr == clr && cx.last_shad == shad)
		{
			auto& b = objs[ cx.last_os ];
			if (b.off + b.count == objs_off)
			{
				b.count += cou;
				objs_off += cou;
				return;
			}
		}
		
		reserve_more_block( cx.cmds, 256 );
		
		if (cx.last_shad != shad)
		{
			cx.last_shad = shad;
			cx.last_clr = clr; // shader param, so must be updated for new shader
			
			cx.cmds.push_back({ Cmd::T_SHAD, shad });
			cx.cmds.push_back({ Cmd::T_CLR, clr });
		}
		else
		{
			if (cx.last_clr != clr)
			{
				cx.last_clr = clr;
				cx.cmds.push_back({ Cmd::T_CLR, clr });
			}
		}
		
		if (cx.last_tex != tex)
		{
			cx.last_tex = tex;
			cx.cmds.push_back({ Cmd::T_TEX, tex });
		}
		
		// add object
		
		cx.cmds.push_back({ Cmd::T_OBJ, objs.size() });
		reserve_more_block( objs, 256 );
		
		auto& e = objs.emplace_back();
		e.off = objs_off;
		e.count = cou;
		
		objs_off += cou;
	}
	void reserve( int rect_count )
	{
		reserve_more_block( data, std::max( rect_count, 400 * (4*6) ));
	}
	
	void add_rect (const Rectfp& pos, float cs, float sn, const Rectfp& tc)
	{
		auto hsz = pos.size() / 2;
		auto org = pos.lower();
		
		float xd = cs * hsz.x - sn * hsz.y;
		float yd = sn * hsz.x + cs * hsz.y;
		
		float zd = cs * hsz.x + sn * hsz.y;
		float wd = sn * hsz.x - cs * hsz.y;
		
		float x0 = org.x - xd; float y0 = org.y - yd;
		float x1 = org.x + zd; float y1 = org.y + wd;
		float x2 = org.x - zd; float y2 = org.y - wd;
		float x3 = org.x + xd; float y3 = org.y + yd;
		
		add_rect(x0, y0, x1, y1, x2, y2, x3, y3, tc);
	}
	void add_rect (const Rectfp& pos, const Rectfp& tc)
	{
		add_rect(pos.lower().x, pos.lower().y, pos.upper().x, pos.upper().y, tc);
	}
	void add_line (vec2fp p0, vec2fp p1, float width)
	{
		if (p0.equals(p1, 1e-5f)) return; // otherwise fastlen will fail
		
		vec2fp n = p1 - p0;
		n.rot90cw();
		n *= width * 0.5f / n.fastlen();
		
		add_rect(p0.x + n.x, p0.y + n.y,
		         p1.x + n.x, p1.y + n.y,
		         p0.x - n.x, p0.y - n.y,
		         p1.x - n.x, p1.y - n.y,
		         white_tc);
	}
	
#define IS_TEXT_WHITE true
	
	
	RenImm_Impl()
	{
		auto w = RenText::get().get_white_rect();
		white_tex = w.get_obj();
		white_tc = w.tc;
		
		vao.set_buffers({ std::make_shared<GLA_Buffer>(4) });
		vao.bufs[0]->usage = GL_STREAM_DRAW;
		
		ctxs.resize(DEFCTX_NONE);
		Context* cx;
		
//		cx = &ctxs[DEFCTX_BACK].ctx;
//		cx->sh      = RenderControl::get().load_shader("imm");
//		cx->sh_text = RenderControl::get().load_shader("imm_text");
//		cx->cam = RenderControl::get().get_world_camera();
		
		cx = &ctxs[DEFCTX_WORLD].ctx;
		cx->sh      = RenderControl::get().load_shader("imm");
		cx->sh_text = RenderControl::get().load_shader("imm_text");
		cx->cam = RenderControl::get().get_world_camera();
		
		cx = &ctxs[DEFCTX_UI].ctx;
		cx->sh      = RenderControl::get().load_shader("imm");
		cx->sh_text = RenderControl::get().load_shader("imm_text");
		cx->cam = RenderControl::get().get_ui_camera();
	}
	Context& get_context (CtxIndex id)
	{
		if (id == DEFCTX_NONE) LOG_THROW_X("RenImm::get_context() invalid id");
		return ctxs[id].ctx;
	}
	void set_context (CtxIndex id)
	{
		ctx_cur = id;
		
		if (id == DEFCTX_NONE) ctx_ok = false; // and it doesn't matter that index is invalid
		else {
			auto &c = ctxs[ ctx_cur ].ctx;
			ctx_ok = ( c.cam && c.sh && c.sh->get_obj() );
		}
	}
	
	
	
	void draw_rect (const Rectfp& dst, uint32_t clr)
	{
		if (!can_add( IS_TEXT_WHITE )) return;
		reserve(1);
		
		add_rect( dst, white_tc );
		add_obj( white_tex, clr, IS_TEXT_WHITE );
	}
	void draw_frame (const Rectfp& dst, uint32_t clr, float frame_width)
	{
		if (!can_add( IS_TEXT_WHITE )) return;
		reserve(4);
		
		auto x0 = dst.lower().x;
		auto y0 = dst.lower().y;
		auto x1 = dst.upper().x;
		auto y1 = dst.upper().y;
		
		add_line({x0, y0}, {x1, y0}, frame_width);
		add_line({x0, y0}, {x0, y1}, frame_width);
		add_line({x1, y1}, {x1, y0}, frame_width);
		add_line({x1, y1}, {x0, y1}, frame_width);
		add_obj( white_tex, clr, IS_TEXT_WHITE );
	}
	void draw_image (const Rectfp& dst, const TextureReg& tex, uint32_t clr)
	{
		if (uint t = tex.get_obj())
		draw_image (t, dst, tex.tc, clr);
	}
	void draw_image (uint32_t tex, const Rectfp& dst, const Rectfp& src, uint32_t clr)
	{
		if (!can_add( false )) return;
		reserve(1);
		
		add_rect( dst, src );
		add_obj( tex, clr, false );
	}
	
	
	
	void draw_rect_rot (const Rectfp& dst, uint32_t clr, float rot)
	{
		if (!can_add( IS_TEXT_WHITE )) return;
		reserve(1);
		
		auto rk = cossin_ft(rot);
		add_rect(dst, rk.x, rk.y, white_tc);
		add_obj(white_tex, clr, IS_TEXT_WHITE);
	}
	void draw_frame_rot (const Rectfp& dst, uint32_t clr, float rot, float frame_width)
	{
		if (!can_add( IS_TEXT_WHITE )) return;
		reserve(4);
		
		auto x0 = dst.lower().x;
		auto y0 = dst.lower().y;
		
		auto sz = dst.size();
		auto rk = cossin_ft(rot);
		auto x1 = x0 + sz.x * rk.x;
		auto y1 = y0 + sz.y * rk.y;
		
		add_line({x0, y0}, {x1, y0}, frame_width);
		add_line({x0, y0}, {x0, y1}, frame_width);
		add_line({x1, y1}, {x1, y0}, frame_width);
		add_line({x1, y1}, {x0, y1}, frame_width);
		add_obj(white_tex, clr, IS_TEXT_WHITE);
	}
	void draw_image_rot (const Rectfp& dst, const TextureReg& tex, float rot, uint32_t clr)
	{
		if (uint t = tex.get_obj())
		draw_image_rot (t, dst, tex.tc, rot, clr);
	}
	void draw_image_rot (uint32_t tex, const Rectfp& dst, const Rectfp& src, float rot, uint32_t clr)
	{
		if (!can_add( false )) return;
		reserve(1);
		
		auto rk = cossin_ft( rot );
		add_rect( dst, rk.x, rk.y, src );
		add_obj( tex, clr, false );
	}
	
	
	
	void draw_line (vec2fp p0, vec2fp p1, uint32_t clr, float width)
	{
		if (!can_add( IS_TEXT_WHITE )) return;
		reserve(1);
		
		add_line(p0, p1, width);
		add_obj(white_tex, clr, IS_TEXT_WHITE);
	}
	void draw_radius (vec2fp pos, float radius, uint32_t clr, float width, int segn)
	{
		if (!can_add( IS_TEXT_WHITE )) return;
		
		const int seg_min = 8;
		const int seg_max = 64;
		const int seg_per = 400;
		
		float len = M_PI * radius * radius;
		if (segn < 0) segn = len / seg_per;
		if (segn < seg_min) segn = seg_min;
		if (segn > seg_max) segn = seg_max;
		
		vec2fp segs[seg_max];
		for (int i=0; i<segn; i++) {
			float a = M_PI*2 / segn * i;
			segs[i].set(radius, 0);
			segs[i].fastrotate(a);
			segs[i] += pos;
		}
		
		reserve( segn );
		for (int i=1; i<segn; i++) add_line(segs[i-1], segs[i], width);
		add_line(segs[segn-1], segs[0], width);
		add_obj(white_tex, clr, IS_TEXT_WHITE);
	}
	void draw_circle (vec2fp pos, float radius, uint32_t clr, int segn)
	{
		if (!can_add( IS_TEXT_WHITE )) return;
		
		const int seg_min = 8;
		const int seg_max = 64;
		const int seg_per = 400;
		
		float len = M_PI * radius * radius;
		if (segn < 0) segn = len / seg_per;
		if (segn < seg_min) segn = seg_min;
		if (segn > seg_max) segn = seg_max;
		
		vec2fp segs[seg_max];
		for (int i=0; i<segn; i++) {
			float a = M_PI*2 / segn * i;
			segs[i].set(radius, 0);
			segs[i].fastrotate(a);
			segs[i] += pos;
		}
		
		reserve( segn / 2 );
		
		auto add = [&](vec2fp p1, vec2fp p2)
		{
			data.push_back( pos.x ); data.push_back( pos.y );
			data.push_back( white_tc.center().x );
			data.push_back( white_tc.center().y );
			
			data.push_back( p1.x ); data.push_back( p1.y );
			data.push_back( white_tc.center().x );
			data.push_back( white_tc.center().y );
			
			data.push_back( p2.x ); data.push_back( p2.y );
			data.push_back( white_tc.center().x );
			data.push_back( white_tc.center().y );
		};
		
		for (int i=1; i<segn; i++) add(segs[i-1], segs[i]);
		add(segs[segn-1], segs[0]);
		add_obj(white_tex, clr, IS_TEXT_WHITE);
	}

	void draw_text (vec2fp at, TextRenderInfo& tri, uint32_t clr, bool center, float size_k)
	{
		if (tri.cs.empty()) return;
		if (!can_add( true )) return;
		if (center) at -= vec2fp(tri.size) * size_k / 2;
		
		// reduce number of texture switches for multiple atlases
		auto cmp = [](auto &&a, auto &&b) {return a.tex.get_obj() < b.tex.get_obj();};
		if (!std::is_sorted( tri.cs.begin(), tri.cs.end(), cmp ))
			std::sort( tri.cs.begin(), tri.cs.end(), cmp );
		
		auto prev = tri.cs.front().tex.tex;
		
		reserve( tri.cs.size() );
		for (auto &c : tri.cs)
		{
			if (prev != c.tex.tex)
			{
				add_obj( prev->get_obj(), clr, true );
				prev = c.tex.tex;
			}
			vec2fp p = c.pos.lower();
			add_rect({ p * size_k + at, vec2fp(c.pos.size()) * size_k, true }, c.tex.tc);
		}
		add_obj( prev->get_obj(), clr, true );
	}
	void draw_text (vec2fp at, std::string_view str, uint32_t clr, bool center, float size_k, FontIndex font)
	{
		if (!can_add( true )) return;

		TextRenderInfo tri;
		tri.str_a = str.data();
		tri.length = str.length();
		tri.font = font;
		tri.build();
		draw_text( at, tri, clr, center, size_k );
	}
	void draw_text (vec2fp at, std::vector<std::pair<std::string, FColor>> strs)
	{
		if (!can_add(true) || strs.empty()) return;
		const float size_k = 1.f;
		
		std::string tmp;
		tmp.reserve(4096);
		for (size_t si = 0; si < strs.size(); )
		{
			auto& s = strs[si].first;
			tmp += s;
			
			size_t i = 0;
			while (true) {
				i = s.find('\n', i);
				if (i == std::string::npos) break;
				s.erase(i);
			}
			if (s.empty()) strs.erase( strs.begin() + si );
			else ++si;
		}
		
		TextRenderInfo tri;
		tri.str_a = tmp.data();
		tri.length = tmp.length();
		tri.build();
		if (tri.cs.empty()) return;
		
		auto prev = tri.cs.front().tex.tex;
		size_t c_cou = 0;
		size_t clr_i = 0;
		
		reserve( tri.cs.size() );
		for (auto &c : tri.cs)
		{
			if (prev != c.tex.tex || c_cou == strs[clr_i].first.length())
			{
				add_obj( prev->get_obj(), strs[clr_i].second.to_px(), true );
				prev = c.tex.tex;
				
				if (c_cou == strs[clr_i].first.length() && clr_i != strs.size() - 1) {
					++clr_i;
					c_cou = 0;
				}
			}
			
			vec2fp p = c.pos.lower();
			add_rect({ p * size_k + at, vec2fp(c.pos.size()) * size_k, true }, c.tex.tc);
			++c_cou;
		}
		add_obj( prev->get_obj(), strs[clr_i].second.to_px(), true );
	}
	void draw_text_hud (vec2fp at, std::string_view str, uint32_t clr, bool centered, float size_k)
	{
		const int alpha = 0x80;
		const int border = 4;
		
		if (!can_add(true)) return;
		
		TextRenderInfo tri;
		tri.str_a = str.data();
		tri.length = str.length();
		tri.build();
		if (tri.cs.empty()) return;
		
		tri.size += vec2i::one(border * 2);
		auto sz = RenderControl::get_size();
		if (at.x < 0) at.x += sz.x - tri.size.x;
		if (at.y < 0) at.y += sz.y - tri.size.y;
		
		if (centered) draw_rect({at - tri.size/2, tri.size, true}, alpha);
		else {
			draw_rect({at, tri.size, true}, alpha);
			at += vec2fp::one(border);
		}
		draw_text(at, tri, clr, centered, size_k);
	}
	void draw_vertices(const std::vector<vec2fp>& vs)
	{
		reserve(vs.size() / 6);
		for (auto& v : vs) {
			data.push_back(v.x);
			data.push_back(v.y);
			data.push_back(white_tc.center().x);
			data.push_back(white_tc.center().y);
		}
	}
	void draw_vertices_end(uint32_t clr)
	{
		add_obj( white_tex, clr, IS_TEXT_WHITE );
	}
	void clip_push( Rect r )
	{
		if (!ctx_ok) return;
		auto& cx = ctxs[ ctx_cur ];
		
		if (!cx.clip_stack.empty()) r = calc_intersection( r, cx.clip_stack.back().first );
		
		size_t ix = clips.size();
		reserve_more_block( clips, 32 );
		clips.push_back( r );
		
		reserve_more_block( cx.clip_stack, 32 );
		cx.clip_stack.push_back({ r, ix });
		
		reserve_more_block( cx.cmds, 256 );
		cx.cmds.push_back({ Cmd::T_CLIP, ix });
	}
	void clip_pop()
	{
		if (!ctx_ok) return;
		auto& cx = ctxs[ ctx_cur ];
		
		if (cx.clip_stack.empty()) return;
		cx.clip_stack.pop_back();
		
		reserve_more_block( cx.cmds, 256 );
		if (cx.clip_stack.empty()) cx.cmds.push_back({ Cmd::T_CLIP, INVIX });
		else                       cx.cmds.push_back({ Cmd::T_CLIP, cx.clip_stack.back().second });
	}
	void draw_cmd( EffectCmd cmd )
	{
		if (!ctx_ok) return;
		auto& cx = ctxs[ ctx_cur ];
		
		reserve_more_block( cx.cmds, 256 );
		cx.cmds.push_back({ Cmd::T_ECMD, static_cast<size_t>(cmd) });
	}
	void render_pre()
	{
		vao.bufs[0]->update( data.size(), data.data() );
//		vao.bind();
		glActiveTexture( GL_TEXTURE0 );
		
		glGetIntegerv( GL_VIEWPORT, vp );
	}
	void render(CtxIndex cx_id)
	{
		auto& cx = ctxs[cx_id];
		if (cx.cmds.empty()) return;
		auto& ps = cx.ctx;
		
		Shader* sh = nullptr;
		const float *mx = ps.cam->get_full_matrix();
		
		vao.bind();
		
		for (auto& cmd : cx.cmds)
		{
			switch (cmd.type)
			{
			case Cmd::T_OBJ:
				{	auto& obj = objs[ cmd.index ];
					glDrawArrays( GL_TRIANGLES, obj.off, obj.count );
				}
				break;
				
			case Cmd::T_CLIP:
				if (cmd.index == INVIX) glScissor( vp[0], vp[1], vp[2], vp[3] );
				else
				{	auto& r = clips[ cmd.index ];
					int y = RenderControl::get().get_size().y - (r.lower().y + r.size().y);
					glScissor( r.lower().x, y, r.size().x, r.size().y );
				}
				break;
				
			case Cmd::T_CLR:
				sh->set_rgba( "clr", cmd.index );
				break;
				
			case Cmd::T_TEX:
				glBindTexture( GL_TEXTURE_2D, cmd.index );
				break;
				
			case Cmd::T_SHAD:
				sh = (cmd.index == SHAD_MAIN ? ps.sh : ps.sh_text);
				sh->bind();
				sh->set4mx( "proj", mx );
				break;
				
			case Cmd::T_ECMD:
				switch (static_cast< EffectCmd >( cmd.index ))
				{
				case EFF_POP:
					glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
					break;
					
				case EFF_ADDITIVE_BLEND:
					glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
					break;
				}
				break;
			}
		}
	}
	void render_post()
	{
		glScissor( vp[0], vp[1], vp[2], vp[3] );
		
		data.clear();
		objs_off = 0;
		
		objs.clear();
		clips.clear();
		
		size_t id = 0;
		for (auto &c : ctxs)
		{
			++id;
			if (!c.clip_stack.empty())
				VLOGD( "RenImm::render() clip stack not empty - ctx ID {}", id );
			
			c.cmds.clear();
			c.clip_stack.clear();
			
			c.last_os = INVIX;
			c.last_tex = 0;
			c.last_shad = SHAD_INITIAL;
		}
		ctx_ok = false;
	}
};



static RenImm_Impl* rni;
RenImm& RenImm::get() {
	if (!rni) LOG_THROW_X("RenImm::get() null");
	return *rni;
}
RenImm* RenImm::init() {return rni = new RenImm_Impl;}
RenImm::~RenImm() {rni = nullptr;}
