#include "core/vig.hpp"
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
	ax = -vx / (ft + lt);
	ay = -vy / (ft + lt);
	ar = -vr / (ft + lt);
}
void ParticleGroupGenerator::draw(const ParticleBatchPars& pars)
{
	ParticleRenderer::get().add(*this, pars);
}



size_t ParticleGroupStd::begin(const ParticleBatchPars &pars, ParticleParams& p)
{
	t_tr = pars.tr;
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
	p.pr = t_tr.rot + rnd_stat().range(rot_min, rot_max);
	vec2fp rv = {radius_fixed? radius : (float) rnd_stat().range(0, radius), 0.f};
	rv.rotate(p.pr);
	p.px = t_tr.pos.x + rv.x;
	p.py = t_tr.pos.y + rv.y;
	
	// speed
	float sp = rnd_stat().range(speed_min, t_spdmax);
	float sa = rnd_stat().range(rot_min, rot_max);
	p.vx = sp * cos(sa);
	p.vy = sp * sin(sa);
	
	p.vr = rnd_stat().range(rot_speed_min, t_rotmax);
	if (rnd_stat().range(-1, 1) < 0) p.vr = -p.vr;
	
	// color
	if (colors.size()) {
		int i = rnd_stat().range_index(colors.size());
		p.clr[0] = (colors[i] >> 24) / 255.f;
		p.clr[1] = (colors[i] >> 16) / 255.f;
		p.clr[2] = (colors[i] >> 8) / 255.f;
		if (alpha) p.clr[3] = alpha / 255.f;
		else p.clr[3] = colors[i] / 255.f;
	}
	else {
		for (int i=0; i<3; i++) p.clr[i] = rnd_stat().range(colors_range[i], colors_range[i+3]) / 255.f;
		p.clr[3] = alpha / 255.f;
	}
	if (color_speed != 0.f) {
		for (int j=0; j<3; j++)
			p.clr[j] *= (1. - color_speed);
	}
	
	// time
	p.lt = rnd_stat().range(TTL.seconds(), t_lmax);
	p.ft = rnd_stat().range( FT.seconds(), t_fmax);
}



class ParticleRenderer_Impl : public ParticleRenderer
{
public:
	const int per_part = 5*4; // values per particle
	const int part_lim_step = 10000;
	int part_lim = 0; // max number of particles
	
	GLA_VertexArray vao[2];
	std::shared_ptr<GLA_Buffer> bufs[2];
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
		auto load_calc = [this]()
		{
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
		};
		
		for (int i=0; i<2; ++i) {
			bufs[i].reset( new GLA_Buffer(0) );
			vao[i].set_attribs(std::vector<GLA_VertexArray::Attrib>(5, {bufs[i], 4}));
		}
		
		resize_bufs(0);
		sh_draw = RenderControl::get().load_shader("ps_draw", [&](Shader&){load_calc();});
		
		dbgm_g = vig_reg_menu(VigMenu::DebugRenderer, [this](){vig_label_a("Particles: {:5} / {:5}\n", gs_off_max, part_lim);});
	}
	void resize_bufs(int additional)
	{
		int new_lim = part_lim + part_lim_step * (1 + additional / part_lim_step);
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
		
		vao[0].bind();
		glEnable(GL_RASTERIZER_DISCARD);
		glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, vao[1].bufs[0]->vbo);
		glBeginTransformFeedback(GL_POINTS);
		
		int ptr = 0, l_off, l_num = 0;
		for (size_t gi=0; gi<gs.size(); gi++)
		{
			Group& g = gs[gi];
			
			g.time_left -= passed;
			if (g.time_left.is_negative())
			{
				if (l_num) {
					glDrawArrays(GL_POINTS, l_off, l_num);
					l_num = 0;
				}
				
				gs.erase( gs.begin() + gi );
				--gi;
				continue;
			}
			
			if (!l_num) l_off = g.off;
			l_num += g.num;
			
			g.off = ptr;
			ptr += g.num;
		}
		if (l_num) glDrawArrays(GL_POINTS, l_off, l_num);
		gs_off_max = ptr;
		
		glEndTransformFeedback();
		glDisable(GL_RASTERIZER_DISCARD);
		
		vao[0].swap(vao[1]);
	}
	
	
	
	void render()
	{
		if (gs.empty()) return;
		
		update( RenderControl::get().get_passed() );
		if (!gs_off_max) return;
		
		sh_draw->bind();
		sh_draw->set4mx("proj", RenderControl::get().get_world_camera()->get_full_matrix());
		
		vao[0].bind();
		glDrawArrays(GL_POINTS, 0, gs_off_max);
	}
	void add(ParticleGroupGenerator& group, const ParticleBatchPars& pars)
	{
		ParticleParams p;
		p.pr = p.vr = 0;
		p.ax = p.ay = p.ar = 0;
		
		int num = group.begin(pars, p);
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
			
			// color (const)
			d[0] = p.clr.r;
			d[1] = p.clr.g;
			d[2] = p.clr.b;
			d[3] = p.clr.a;
		}
		
		// finish
		
		vao[0].bufs[0]->update_part(gs.back().off * per_part, num * per_part, data);
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
