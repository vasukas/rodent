#include "core/tui_layer.hpp"
#include "vaslib/vas_log.hpp"
#include "camera.hpp"
#include "control.hpp"
#include "ren_text.hpp"
#include "ren_tui.hpp"
#include "shader.hpp"

class RenTUI_Impl : public RenTUI
{
public:
	GLA_VertexArray vao;
	Shader* sh;
	
	vec2i size; // display size in characters
	vec2i cz; // char size
	
	std::vector<TUI_Char> oldbuf;
	TUI_Surface sur;
	
	const int attr = 8; ///< attributes (single float) per vertex
	const int vert = 6; ///< vertex per character
	int num; // half size of buffer, in floats
	
	std::vector<GLfloat> data; ///< buffer data (client-side)
	Texture* tex;
	vec2fp wr;
	
	bool force_upd = true;
	
	
	// coord transform (screen pixels -> NDC)
	float sx, sy;
	void trans(float &x, float &y) {
		x *= sx; x -= 1;
		y *= sy; y = 1 - y;
	}
	uint32_t getclr(uint32_t c, bool fore)
	{
		if (c < 8)
		{
			// black, red, green, yellow
			// blue, magenta, cyan, white
			static const uint32_t tab[16] = 
			{
			    // back
			    0,
			    0xa00000ff,
			    0x00a000ff,
			    0xa09000ff,
			    
			    0x000090ff,
			    0xa00090ff,
			    0x00a0a0ff,
			    0xa0a0a0ff,
			    
			    // fore
			    0x202020ff,
			    0xff1010ff,
			    0x00ff20ff,
			    0xfff000ff,
			    
			    0x4040ffff,
			    0xff20ffff,
			    0x00ffa0ff,
			    0xf0f0f0ff
			};
			return tab[c + (8 * fore)];
		}
		switch (c)
		{
		case TUI_SET_TEXT: return getclr(TUI_GREEN, true);
		case TUI_SET_BACK: return getclr(TUI_BLACK, false);
		}
		return 0xffffffff;
	}
	
	
	RenTUI_Impl()
	{
		auto b = std::make_shared<GLA_Buffer>(4);
		vao.set_attribs({ {b, 4}, {b, 4} });
		sh = RenderControl::get().load_shader("char_matrix");
		
		
		size = TUI_Layer::screen_size();
		cz = RenText::get().mxc_size(FontIndex::TUI);
		
		oldbuf.resize( size.area() );
		sur.resize_clear(size);
		
		auto wg = RenText::get().get_white_rect();
		tex = wg.tex;
		wr = wg.tc.lower();
		
		
		num = size.area() * attr * vert; // half size of buffer, in floats
		data.resize( num*2 );
		
		sx = 2.f / (size.x * cz.x);
		sy = 2.f / (size.y * cz.y);
		
		// init data - set background rectangles' position and texcoords
		for (int i=0; i < size.area(); ++i) {
			int y = i / size.x;
			int x = i % size.x;
			
			float x0 = x * cz.x; // get screen coords
			float y0 = y * cz.y;
			float x1 = x0 + cz.x;
			float y1 = y0 + cz.y;
			trans(x0, y0); // transform to NDC
			trans(x1, y1);
			
			// set NDC position and texcoord
			float *d = data.data() + i*vert*attr;
			d[0] = x0, d[1] = y0, d[2] = wr.x, d[3] = wr.y; d += 8;
			d[0] = x0, d[1] = y1, d[2] = wr.x, d[3] = wr.y; d += 8;
			d[0] = x1, d[1] = y0, d[2] = wr.x, d[3] = wr.y; d += 8;
			d[0] = x0, d[1] = y1, d[2] = wr.x, d[3] = wr.y; d += 8;
			d[0] = x1, d[1] = y0, d[2] = wr.x, d[3] = wr.y; d += 8;
			d[0] = x1, d[1] = y1, d[2] = wr.x, d[3] = wr.y; d += 8;
		}
//		// set alpha to one for all vertices
//		for (int i=7; i < num*2; i += attr) data[i] = 1.f;
	}
	void render()
	{
		if (TUI_Layer::render_all(sur) || force_upd)
		{
			bool update = false;
			
			for (size_t i = 0; i < oldbuf.size(); ++i)
			{
				auto& ob = oldbuf[i];
				auto& bc = sur.cs[i];
				bool achx = (ob.alpha != bc.alpha) || force_upd;
				
				if (ob.sym != bc.sym || force_upd) {
					ob.sym = bc.sym;
					update = true;
					
					auto cd = RenText::get().get_glyph(ob.sym? ob.sym : ' ', FontIndex::TUI);
					vec2i pos( i % size.x, i / size.x );
					
					if (tex != cd.tex.tex)
						cd.tex.tc = {wr, wr, false};
					
					// calculate coordinates for character
					float x0 = pos.x * cz.x + cd.pos.lower().x; // get screen coords
					float y0 = pos.y * cz.y + cd.pos.lower().y;
					float x1 = x0 + cd.pos.size().x;
					float y1 = y0 + cd.pos.size().y;
					trans(x0, y0); // transform to NDC
					trans(x1, y1);
					
					// set NDC position and texcoord
					float *d = data.data() + i * vert*attr + num;
					d[0] = x0, d[1] = y0, d[2] = cd.tex.tc.a.x, d[3] = cd.tex.tc.a.y; d += 8;
					d[0] = x0, d[1] = y1, d[2] = cd.tex.tc.a.x, d[3] = cd.tex.tc.b.y; d += 8;
					d[0] = x1, d[1] = y0, d[2] = cd.tex.tc.b.x, d[3] = cd.tex.tc.a.y; d += 8;
					d[0] = x0, d[1] = y1, d[2] = cd.tex.tc.a.x, d[3] = cd.tex.tc.b.y; d += 8;
					d[0] = x1, d[1] = y0, d[2] = cd.tex.tc.b.x, d[3] = cd.tex.tc.a.y; d += 8;
					d[0] = x1, d[1] = y1, d[2] = cd.tex.tc.b.x, d[3] = cd.tex.tc.b.y; d += 8;
				}
				if (ob.fore != bc.fore || achx) {
					ob.fore = bc.fore;
					ob.alpha = bc.alpha;
					update = true;
					
					// calculate normalized color
					uint32_t clr = getclr(bc.fore, true);
					float r = ((clr >> 16) & 0xff) / 255.f;
					float g = ((clr >> 8) & 0xff) / 255.f;
					float b = ((clr) & 0xff) / 255.f;
					float a = bc.alpha;
					
					// set color
					float *d = data.data() + i * vert*attr + num;
					d[4] = r, d[5] = g, d[6] = b; d[7] = a; d += 8;
					d[4] = r, d[5] = g, d[6] = b; d[7] = a; d += 8;
					d[4] = r, d[5] = g, d[6] = b; d[7] = a; d += 8;
					d[4] = r, d[5] = g, d[6] = b; d[7] = a; d += 8;
					d[4] = r, d[5] = g, d[6] = b; d[7] = a; d += 8;
					d[4] = r, d[5] = g, d[6] = b; d[7] = a; d += 8;
				}
				if (ob.back != bc.back || achx) {
					ob.back = bc.back;
					ob.alpha = bc.alpha;
					update = true;
					
					// calculate normalized color
					uint32_t clr = getclr(bc.back, false);
					float r = ((clr >> 16) & 0xff) / 255.f;
					float g = ((clr >> 8) & 0xff) / 255.f;
					float b = ((clr) & 0xff) / 255.f;
					float a = bc.alpha;
					
					// set color
					float *d = data.data() + i * vert*attr;
					d[4] = r, d[5] = g, d[6] = b; d[7] = a; d += 8;
					d[4] = r, d[5] = g, d[6] = b; d[7] = a; d += 8;
					d[4] = r, d[5] = g, d[6] = b; d[7] = a; d += 8;
					d[4] = r, d[5] = g, d[6] = b; d[7] = a; d += 8;
					d[4] = r, d[5] = g, d[6] = b; d[7] = a; d += 8;
					d[4] = r, d[5] = g, d[6] = b; d[7] = a; d += 8;
				}
			}
			
			if (update)
				vao.bufs[0]->update(num*2, data.data());
			
			force_upd = false;
		}
		
		sh->bind();
		
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex->get_obj());
		
		vao.bind();
		glDrawArrays(GL_TRIANGLES, 0, num); // draw background rectangles (fills window completely, so no clear)
		glDrawArrays(GL_TRIANGLES, num, num); // draw characters
	}
};



static RenTUI_Impl* rni;
RenTUI& RenTUI::get() {
	if (!rni) LOG_THROW_X("RenTUI::get() null");
	return *rni;
}
RenTUI* RenTUI::init() {return rni = new RenTUI_Impl;}
RenTUI::~RenTUI() {rni = nullptr;}
