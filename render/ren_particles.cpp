#include "core/vig.hpp"
#include "utils/noise.hpp"
#include "vaslib/vas_log.hpp"
#include "camera.hpp"
#include "control.hpp"
#include "ren_particles.hpp"
#include "shader.hpp"



void ParticleParams::set_zero(bool vel, bool accel)
{
	if (vel) vel = {};
	if (accel) acc = {};
}
void ParticleParams::decel_to_zero()
{
	acc = -vel / (lt + ft);
}
void ParticleParams::apply_gravity(float height, float t_fall)
{
	float t1 = (lt + ft) * (1 + t_fall);
	float t0 = t1 / 2; // max height time
	vel.y += 4 * -height / t1;
	acc.y += -vel.y / t0;
}
void ParticleGroupGenerator::draw(const ParticleBatchPars& pars)
{
	RenParticles::get().add(*this, pars);
}



class RenParticles_Impl : public RenParticles
{
public:
	const int per_part = 5*4; // values per particle
	const int part_lim_step = 10000;
	int part_lim = 0; // max number of particles
	
	GLA_VertexArray vao[2];
	std::shared_ptr<GLA_Buffer> bufs[2];
	std::vector<float> upd_data; // buffer data
	
	std::unique_ptr<Shader> sh_calc, sh_draw;
	
	struct Group {
		int off, num;
		TimeSpan time_left;
	};
	
	std::vector<Group> gs;
	int gs_off_max = 0; // max currently used
	
	RAII_Guard dbgm_g;
	
	
	
	RenParticles_Impl()
	{
		Shader::Callbacks cbs;
		cbs.pre_link = [](Shader& sh)
		{
			const char *vars[] = {
			    "newd[0]",
			    "newd[1]",
			    "newd[2]",
			    "newd[3]",
			    "newd[4]"
			};
			glTransformFeedbackVaryings(sh.get_prog(), 5, vars, GL_INTERLEAVED_ATTRIBS);
		};
		sh_calc = Shader::load("ps_calc", std::move(cbs));
		
		for (int i=0; i<2; ++i) {
			bufs[i].reset( new GLA_Buffer(0) );
			vao[i].set_attribs(std::vector<GLA_VertexArray::Attrib>(5, {bufs[i], 4}));
		}
		
		resize_bufs(0);
		sh_draw = Shader::load("ps_draw", {}, true);
		
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
		auto passed = RenderControl::get().get_passed();
		
		update(passed);
		if (!gs_off_max) return;
		if (!enabled) return;
		
		sh_draw->bind();
		sh_draw->set4mx("proj", RenderControl::get().get_world_camera().get_full_matrix());
		
		vao[0].bind();
		glDrawArrays(GL_POINTS, 0, gs_off_max);
	}
	void add(ParticleGroupGenerator& group, const ParticleBatchPars& pars)
	{
		ParticleParams p;
		p.acc = {};
		
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
			
			// pos, vel
			d[0] = p.pos.x;
			d[1] = p.pos.y;
			d[2] = p.vel.x;
			d[3] = p.vel.y;
			d += 4;
			
			// left, size
			d[0] = total;
			d[1] = p.size;
//			d[2] = 0;
//			d[3] = 0;
			d += 8; // +4 color calculated in shader
			
			// acc, fade, eid
			d[0] = p.acc.x;
			d[1] = p.acc.y;
			d[2] = 1.f / p.ft;
//			d[3] = 0;
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



static RenParticles_Impl* rni;
RenParticles& RenParticles::get() {return *rni;}
RenParticles* RenParticles::init() {return rni = new RenParticles_Impl;}
RenParticles::~RenParticles() {rni = nullptr;}
