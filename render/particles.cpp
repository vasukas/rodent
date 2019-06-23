#include "core/dbg_menu.hpp"
#include "utils/noise.hpp"
#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_file.hpp"
#include "vaslib/vas_log.hpp"
#include "camera.hpp"
#include "control.hpp"
#include "particles.hpp"
#include "shader.hpp"



void ParticleParams::set_zero(bool vel, bool accel)
{
	if (vel) vx = vy = vr = 0;
	if (accel) ax = ay = ar = 0;
}
void ParticleParams::decel_to_zero()
{
	ax = vx / (ft + lt);
	ay = vy / (ft + lt);
	ar = vr / (ft + lt);
}
void ParticleGroupGenerator::draw(const Transform& tr, float power)
{
	ParticleRenderer::get().add(*this, tr, power);
}



size_t ParticleGroupStd::begin(const Transform& tr, ParticleParams& p, float)
{
	t_tr = tr;
	t_spdmax = speed_max < 0 ? speed_min : speed_max;
	t_rotmax = rot_speed_max < 0 ? rot_speed_min : rot_speed_max;
	t_lmax = (TTL_max.ms() < 0 ? TTL : TTL_max).seconds();
	t_fmax = (FT_max.ms() < 0 ? FT : FT_max).seconds();
	
	p.set_zero(false, true);
	p.size = px_radius;
	return count;
}
void ParticleGroupStd::gen(ParticleParams& p)
{
	// position
	p.pr = t_tr.rot + rnd_range(rot_min, rot_max);
	vec2fp rv = {radius_fixed? radius : (float) rnd_range(0, radius), 0.f};
	rv.rotate(p.pr);
	p.px = t_tr.pos.x + rv.x;
	p.py = t_tr.pos.y + rv.y;
	
	// speed
	float sp = rnd_range(speed_min, t_spdmax);
	float sa = rnd_range(rot_min, rot_max);
	p.vx = sp * cos(sa);
	p.vy = sp * sin(sa);
	
	p.vr = rnd_range(rot_speed_min, t_rotmax);
	if (rnd_range(-1, 1) < 0) p.vr = -p.vr;
	
	// color
	if (colors.size()) {
		int i = rnd_uint(0, colors.size());
		p.clr[0] = (colors[i] >> 24) / 255.f;
		p.clr[1] = (colors[i] >> 16) / 255.f;
		p.clr[2] = (colors[i] >> 8) / 255.f;
		if (alpha) p.clr[3] = alpha / 255.f;
		else p.clr[3] = colors[i] / 255.f;
	}
	else {
		for (int i=0; i<3; i++) p.clr[i] = rnd_range(colors_range[i], colors_range[i+3]) / 255.f;
		p.clr[3] = alpha / 255.f;
	}
	if (color_speed != 0.f) {
		for (int j=0; j<3; j++)
			p.clr[j] *= (1. - color_speed);
	}
	
	// time
	p.lt = rnd_range(TTL.seconds(), t_lmax);
	p.ft = rnd_range( FT.seconds(), t_fmax);
}



class ParticleRenderer_Impl : public ParticleRenderer
{
public:
	const int part_lim_step = 10000;
	int part_lim = 0; // max number of particles
	int per_part = 20; // values per particle
	
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
		    "newd[4]"
		};
		glTransformFeedbackVaryings(prog, 5, vars, GL_INTERLEAVED_ATTRIBS);
		
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
		
		vao.set_attribs(std::vector<GLA_VertexArray::Attrib>(5, {bufs[0], 4}));
		
		glEnable(GL_RASTERIZER_DISCARD);
		glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, bufs[1]->vbo);
		glBeginTransformFeedback(GL_POINTS);
		
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
		
		vao.set_attribs(std::vector<GLA_VertexArray::Attrib>(5, {bufs[0], 4}));
		glDrawArrays(GL_POINTS, 0, gs_off_max);
	}
	void add(ParticleGroupGenerator& group, const Transform& tr, float power)
	{
		ParticleParams p;
		p.pr = p.vr = 0;
		p.ax = p.ay = p.ar = 0;
		
		int num = group.begin(tr, p, power);
		if (!num) {
			group.end();
			return;
		}
		
		if (gs_off_max + num > part_lim)
			resize_bufs(num);
		
		reserve_more_block(gs, 128);
		gs.push_back({ gs_off_max, num, {} });
		gs_off_max += num;
		
		// fill data
		
		upd_data.clear();
		upd_data.resize(num * per_part);
		float *data = upd_data.data();
		
		float max_time = 0.f;
		
		for (int i=0; i<num; ++i)
		{
			float *d = data + i * per_part;
			group.gen(p);
			
			float total = p.lt + p.ft;
			max_time = std::max(max_time, total);
			
			// pos, left
			d[0] = p.px;
			d[1] = p.py;
			d[2] = p.pr;
			d[3] = total;
			d += 4;
			
			// vel, size
			d[0] = p.vx;
			d[1] = p.vy;
			d[2] = p.vr;
			d[3] = p.size;
// +4 color calculated in shader
			d += 8;
			
			// acc, fade
			d[0] = p.ax;
			d[1] = p.ay;
			d[2] = p.ar;
			d[3] = 1.f / p.ft;
			d += 4;
			
			// color
			d[0] = p.clr.r;
			d[1] = p.clr.g;
			d[2] = p.clr.b;
			d[3] = p.clr.a;
		}
		
		// finish
		
		bufs[0]->update_part(gs.back().off * per_part, num * per_part, data);
		gs.back().time_left.set_seconds(max_time);
		group.end();
	}
};



static ParticleRenderer_Impl* rni;
ParticleRenderer& ParticleRenderer::get() {
	if (!rni) LOG_THROW_X("ParticleRenderer::get() null");
	return *rni;
}
ParticleRenderer* ParticleRenderer::init() {return rni = new ParticleRenderer_Impl;}
ParticleRenderer::~ParticleRenderer() {rni = nullptr;}
