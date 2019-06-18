#include "core/dbg_menu.hpp"
#include "utils/noise.hpp"
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
	void add(const ParticleGroup& g)
	{
		// check
		
		if (!g.count && g.pre_pos.empty()) return;
		if (g.sprs.empty()) {
			debugbreak();
			VLOGE("ParticleRenderer::add() zero sprites");
			return;
		}
		
		Texture* t0 = g.sprs[0].tex;
		for (auto& t : g.sprs) {
			if (t.tex != t0) {
				debugbreak();
				VLOGE("ParticleRenderer::add() multiple textures not supported");
				return;
			}
		}
		
		// setup
		
		auto speed_max = g.speed_max < 0 ? g.speed_min : g.speed_max;
		auto rot_speed_max = g.rot_speed_max < 0 ? g.rot_speed_min : g.rot_speed_max;
		auto TTL_max = g.TTL_max.ms() < 0 ? g.TTL : g.TTL_max;
		auto FT_max = g.FT_max.ms() < 0 ? g.FT : g.FT_max;
		
		// add group
		
		int num = g.pre_pos.empty() ? g.count : g.pre_pos.size();
		if (gs_off_max + num > part_lim)
			resize_bufs(num);
		
		reserve_more_block(gs, 128);
		gs.push_back({ gs_off_max, num, TTL_max + FT_max });
		gs_off_max += num;
		
		tex_obj = g.sprs.front().tex->get_obj();
		
		// fill data
		
		upd_data.reserve(num * per_part);
		float *data = upd_data.data();
		
		for (int i=0; i<num; ++i)
		{
			float *d = data + i * per_part;
			
			// generate
			
			float px, py, pr;
			float vx, vy, vr;
			float ax, ay, ar;
			uint8_t clr[4];
			TimeSpan life_time, fade_time;
			auto& tex = g.sprs[ rnd_uint(0, g.sprs.size()) ];
			
			// position
			pr = rnd_range(g.rot_min, g.rot_max);
			if (g.pre_pos.empty()) {
				vec2i rv = {g.radius_fixed? g.radius : (int) rnd_range(0, g.radius), 0};
				rv.rotate(pr);
				px = g.origin.x + rv.x;
				py = g.origin.y + rv.y;
			}
			else {
				px = g.pre_pos[i].x;
				py = g.pre_pos[i].y;
			}
			
			// speed
			float sp = rnd_range(g.speed_min, speed_max);
			float sa = rnd_range(g.rot_min, g.rot_max);
			vx = sp * cos(sa);
			vy = sp * sin(sa);
			
			vr = rnd_range(g.rot_speed_min, rot_speed_max);
			if (rnd_range(-1, 1) < 0) vr = -vr;
			
			// accel
			ax = ay = ar = 0.f;
			
			// color
			if (g.colors.size()) {
				int i = rnd_uint(0, g.colors.size());
				clr[0] = (g.colors[i] >> 24);
				clr[1] = (g.colors[i] >> 16);
				clr[2] = (g.colors[i] >> 8);
				if (g.alpha) clr[3] = g.alpha;
				else clr[3] = g.colors[i];
			}
			else {
				for (int i=0; i<3; i++) clr[i] = rnd_range(g.colors_range[i], g.colors_range[i+3]);
				clr[3] = g.alpha;
			}
			if (g.color_speed != 0.f) {
				for (int j=0; j<3; j++)
					clr[j] *= (1. - g.color_speed);
			}
			
			// time
			life_time.set_seconds( rnd_range(g.TTL.seconds(), TTL_max.seconds()) );
			fade_time.set_seconds( rnd_range(g.FT .seconds(),  FT_max.seconds()) );
			
			// fill buffer
			
			// pos, left
			d[0] = px;
			d[1] = py;
			d[2] = pr;
			d[3] = (life_time + fade_time).seconds();
			d += 4;
			
			// vel, size
			d[0] = vx;
			d[1] = vy;
			d[2] = vr;
			d[3] = g.px_radius;
			d += 8;
			
			// texlim
			d[0] = tex.tc.lower().x;
			d[1] = tex.tc.lower().y;
			d[2] = tex.tc.upper().x;
			d[3] = tex.tc.upper().y;
			d += 4;
			
			// acc, fade
			d[0] = ax;
			d[1] = ay;
			d[2] = ar;
			d[3] = 1.f / fade_time.seconds();
			d += 4;
			
			// color
			d[0] = float(clr[0]) / 255;
			d[1] = float(clr[1]) / 255;
			d[2] = float(clr[2]) / 255;
			d[3] = float(clr[3]) / 255;
		}
		
		// finish
		
		bufs[0]->update_part(gs.back().off * per_part, num * per_part, data);
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
