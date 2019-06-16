#include "core/dbg_menu.hpp"
#include "core/noise.hpp"
#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_file.hpp"
#include "vaslib/vas_log.hpp"
#include "camera.hpp"
#include "control.hpp"
#include "particles.hpp"
#include "shader.hpp"
#include "texture.hpp"

#include "ren_imm.hpp"


class ParticleRenderer_Impl : public ParticleRenderer
{
public:
	struct InPart
	{
		float px, py, pr; // position
		float vx, vy, vr; // velocity
		float ax, ay, ar; // accelaration
		TimeSpan life_time; ///< TTL before fade
		TimeSpan fade_time; ///< fade length
		uint8_t clr[4]; // wat
		TextureReg tex;
		int srcw2; // src.w / 2
	};
	struct InGroup
	{
		std::vector<InPart> ps;
		TimeSpan lifetime; ///< maximum total TTL for particle
	};
	
	const int part_lim_step = 10000;
	int part_lim = 0; // max number of particles
	int per_part = 24; // values per particle
	
	GLA_VertexArray vao;
	std::array<std::shared_ptr<GLA_Buffer>, 2> bufs;
	std::vector<float> upd_data; // buffer data
	
	std::unique_ptr<Shader> sh_calc;
	Shader* sh_draw = nullptr;
	
	struct Group {
		int off, num;
		TimeSpan time_left;
	};
	
	std::vector<Group> gs;
	int gs_off_max = 0; // max currently used
	
	GLuint tex_obj = 0; // only single texture supported atm
	RAII_Guard dbgm_g;
	
	
	
	ParticleRenderer_Impl()
	{
		bufs[0].reset( new GLA_Buffer(0) );
		bufs[1].reset( new GLA_Buffer(0) );
		resize_bufs(0);
		
		sh_draw = RenderControl::get().load_shader("ps_draw");
		
		// load program
		
		auto src = readfile((Shader::load_path + "ps_calc").c_str());
		GLuint vo = Shader::compile(GL_VERTEX_SHADER, src.value());
		if (!vo) throw std::runtime_error("check log for details");
		
		GLuint prog = glCreateProgram();
		glAttachShader(prog, vo);
		
		const char *vars[] = {
		    "newd[0]",
		    "newd[1]",
		    "newd[2]",
		    "newd[3]",
		    "newd[4]",
		    "newd[5]"
		};
		glTransformFeedbackVaryings(prog, 6, vars, GL_INTERLEAVED_ATTRIBS);
		
		bool ok = Shader::link_fin(prog);
		glDeleteShader(vo);
		if (!ok) throw std::runtime_error("check log for details");
		
		sh_calc.reset(new Shader(prog));
		sh_calc->dbg_name = "ps_calc";
		
		dbgm_g = DbgMenu::get().reg({[this]() {dbgm_label(FMT_FORMAT("{:5} / {:5}", gs_off_max, part_lim));},
		                             "Particles", DBGMEN_RENDER});
	}
	int group_alloc(int size)
	{
		if (gs_off_max + size > part_lim) return -1;
	
		reserve_more_block(gs, 128);
		gs.push_back({ gs_off_max, size, {} });
		
		gs_off_max += size;
		return gs.size() - 1;
	}
	void resize_bufs(int additional)
	{
		int new_lim = part_lim + part_lim_step + additional;
		VLOGD("ParticleRenderer::add_group() limit reached: {} -> {}", part_lim, new_lim);
		
		std::vector<float> tmp;
		tmp.resize(new_lim * per_part);
		
		for (auto& b : bufs)
		{
			if (part_lim) b->get_part(0, part_lim * per_part, tmp.data());
			b->update(new_lim * per_part, tmp.data());
		}
		part_lim = new_lim;
	}
	void add_group(const InGroup& ig)
	{
		if (ig.ps.empty()) return;
		
		int num = ig.ps.size();
		if (gs_off_max + num > part_lim)
			resize_bufs(num);
		
		reserve_more_block(gs, 128);
		gs.push_back({ gs_off_max, num, ig.lifetime });
		gs_off_max += num;
		Group& g = gs.back();
		
		upd_data.reserve(num * per_part);
		float *data = upd_data.data();
		
		tex_obj = ig.ps[0].tex.tex->get_obj();
		
		for (int i=0; i<num; i++)
		{
			float *d = data + i * per_part;
			auto &p = ig.ps[i];
			
			// pos left
			d[0] = p.px;
			d[1] = p.py;
			d[2] = p.pr;
			d[3] = (p.life_time + p.fade_time).seconds();
			d += 4;
			
			// vel size
			d[0] = p.vx;
			d[1] = p.vy;
			d[2] = p.vr;
			d[3] = p.srcw2;
			d += 8;
			
			// texlim
			d[0] = p.tex.tc.lower().x;
			d[1] = p.tex.tc.lower().y;
			d[2] = p.tex.tc.upper().x;
			d[3] = p.tex.tc.upper().y;
			d += 4;
			
			// acc fade
			d[0] = p.ax;
			d[1] = p.ay;
			d[2] = p.ar;
			d[3] = 1.f / p.fade_time.seconds();
			d += 4;
			
			// color
			d[0] = float(p.clr[0]) / 255;
			d[1] = float(p.clr[1]) / 255;
			d[2] = float(p.clr[2]) / 255;
			d[3] = float(p.clr[3]) / 255;
		}
		
		bufs[0]->update_part(g.off * per_part, num * per_part, data);
	}
	void update(TimeSpan passed)
	{
		sh_calc->bind();
		sh_calc->set1f("passed", passed.seconds());
		
		vao.set_attribs(std::vector<GLA_VertexArray::Attrib>(6, {bufs[0], 4}));
		
		glEnable(GL_RASTERIZER_DISCARD);
		glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, bufs[1]->vbo);
		glBeginTransformFeedback(GL_POINTS);
		
		// some old bug test?
//		for (size_t gi=1; gi<gs.size(); gi++) {
//			if (gs[gi].off != gs[gi-1].off + gs[gi-1].num)
//				debugbreak();
//		}
		
		int ptr = 0;
		for (size_t gi=0; gi<gs.size(); gi++) {
			Group& g = gs[gi];
			
			g.time_left -= passed;
			if (g.time_left.ms() < 0) {
				gs.erase( gs.begin() + gi );
				--gi;
				continue;
			}
			
			glDrawArrays(GL_POINTS, g.off, g.num);
			g.off = ptr;
			ptr += g.num;
		}
		gs_off_max = ptr;
		
		glEndTransformFeedback();
		glDisable(GL_RASTERIZER_DISCARD);
		
		bufs[0].swap(bufs[1]);
	}
	
	
	
	void render(TimeSpan passed)
	{
		if (gs.empty()) return;
		
		update(passed);
		
		sh_draw->bind();
		sh_draw->set4mx("proj", RenderControl::get().get_world_camera()->get_full_matrix());
		
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex_obj);
		
		vao.set_attribs(std::vector<GLA_VertexArray::Attrib>(6, {bufs[0], 4}));
		glDrawArrays(GL_POINTS, 0, gs_off_max);
		
//		VLOGD("");
//		float d[per_part];
//		bufs[0]->get_part(0, per_part, d);
//		for (int i=0; i<16; i+=4) VLOGD("{} {} {} {}", d[i], d[i+1], d[i+2], d[i+3]);
	}
	void add(const ParticleGroup& group)
	{
		struct Part {
			TextureReg tex;
			float x, y, rot;
			float xd, yd, rd;
			uint8_t clr[4];
			TimeSpan TTL;
			bool fade;
		};
		struct TmpGroup : ParticleGroup {
			std::vector<Part> ps;
			int active = 0;
			
			bool init()
			{
				if (sprs.empty()) {
					debugbreak();
					VLOGE("ParticleRenderer::add() zero sprites");
					return false;
				}
				Texture* t0 = sprs[0].tex;
				for (auto& t : sprs) {
					if (t.tex != t0) {
						debugbreak();
						VLOGE("ParticleRenderer::add() multiple textures not supported");
						return false;
					}
				}
				
				if (speed_max < 0) speed_max = speed_min;
				if (rot_speed_max < 0) rot_speed_max = rot_speed_min;
				if (TTL_max.ms() < 0) TTL_max = TTL;
				if (FT_max.ms() < 0) FT_max = FT;
				
				ps.resize(count);
				for (auto &p : ps) {
					p.tex = sprs[ rnd_uint(0, sprs.size()) ];
					
					vec2i rv = {radius_fixed? radius : (int) rnd_range(0, radius), 0};
					p.rot = rnd_range(rot_min, rot_max);
					rv.rotate(p.rot);
					p.x = origin.x + rv.x;
					p.y = origin.y + rv.y;
					
					float sp = rnd_range(speed_min, speed_max);
					float sa = rnd_range(rot_min, rot_max);
					p.xd = sp * cos(sa);
					p.yd = sp * sin(sa);
					
//					p.xd = rnd_range(speed_min, speed_max);
//					p.yd = rnd_range(speed_min, speed_max);
					p.rd = rnd_range(rot_speed_min, rot_speed_max);
//					if (rnd_range(-1, 1) < 0) p.xd = -p.xd;
//					if (rnd_range(-1, 1) < 0) p.yd = -p.yd;
					if (rnd_range(-1, 1) < 0) p.rd = -p.rd;
					
					if (colors.size()) {
						int i = rnd_uint(0, colors.size());
						p.clr[0] = (colors[i] >> 24);
						p.clr[1] = (colors[i] >> 16);
						p.clr[2] = (colors[i] >> 8);
						if (alpha) p.clr[3] = alpha;
						else p.clr[3] = colors[i];
					}
					else {
						for (int i=0; i<3; i++) p.clr[i] = rnd_range(colors_range[i], colors_range[i+3]);
						p.clr[3] = alpha;
					}
					
					p.TTL.set_seconds( rnd_range(TTL.seconds(), TTL_max.seconds()) );
					p.fade = false;
				}
			
				active = count;
				return true;
			}
		};
		
		TmpGroup g;
		static_cast<ParticleGroup&>(g) = group;
		if (!g.init()) return;
		
		InGroup ig;
		ig.lifetime = g.TTL_max + g.FT_max;
		ig.ps.resize(g.count);
		for (int i=0; i<g.count; i++)
		{
			auto &p = g.ps[i];
			auto &ip = ig.ps[i];
			
			ip.tex = p.tex;
			ip.srcw2 = g.px_radius;
			
			ip.px = p.x;
			ip.py = p.y;
			ip.pr = p.rot;
			
			ip.vx = p.xd;
			ip.vy = p.yd;
			ip.vr = p.rd;
			
			ip.ax = ip.ay = ip.ar = 0;
			
			ip.life_time = p.TTL;
			ip.fade_time.set_seconds( rnd_range(g.FT.seconds(), g.FT_max.seconds()) );
			
			for (int j=0; j<4; j++) ip.clr[j] = p.clr[j];
			
			if (g.prerender_index == 0 && true) {
				float m = (ip.life_time + ip.fade_time).ms();
				m = -10. / m;
				ip.ax = ip.vx * m;
				ip.ay = ip.vy * m;
				ip.ar = ip.vr * m;
			}
			if (g.color_speed != 0.f) {
				for (int j=0; j<3; j++)
					ip.clr[j] *= (1. - g.color_speed);
			}
		}
		
		add_group(ig);
	}
};



void ParticleGroup::submit() {ParticleRenderer::get().add(*this);}

static ParticleRenderer_Impl* rni;
ParticleRenderer& ParticleRenderer::get() {
	if (!rni) LOG_THROW_X("ParticleRenderer::get() null");
	return *rni;
}
ParticleRenderer* ParticleRenderer::init() {return rni = new ParticleRenderer_Impl;}
ParticleRenderer::~ParticleRenderer() {rni = nullptr;}
