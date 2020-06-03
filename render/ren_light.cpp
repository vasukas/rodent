#include <mutex>
#include "camera.hpp"
#include "control.hpp"
#include "ren_light.hpp"
#include "shader.hpp"
#include "game/common_defs.hpp"
#include "game/level_gen.hpp"
#include "vaslib/vas_containers.hpp"
#include "vaslib/vas_log.hpp"

class RenLight_Impl : public RenLight
{
public:
	struct Light
	{
		int off = -1;
		FColor clr = {};
		std::array<vec2fp, 4> ps = {};
		
		explicit operator bool() const {return off != -1;}
	};
	std::mutex lights_lock;
	SparseArray<Light> lights;
	bool do_update = false;
	
	std::unique_ptr<Shader> sh, sh_mask;
	GLA_Texture tex, mask;
	vec2i mask_size;
	GLA_VertexArray vao;
	std::vector<float> vao_data;
	
	const int tex_size = 128; // width; height is half that
	const int tex_fade = 32; // fade max width
	const float clr_mul = 0.3;
	
	RenLight_Impl()
	{
		sh = Shader::load("light_rect", {});
		sh_mask = Shader::load("light_mask", {});
		
		vao.set_buffers({ std::make_shared<GLA_Buffer>(4) });
		vao.bufs[0]->usage = GL_STATIC_DRAW;
		
		const int height = tex_size/2 + tex_fade;
		std::vector<uint8_t> px;
		px.resize(tex_size * height);
		
		for (int y=0; y<height; ++y)
		for (int x=0; x<tex_size; ++x)
		{
			float yd = float(y - tex_fade) / (y < tex_fade ? tex_fade : tex_size/2);
			float xd = float(x - tex_size/2) / (tex_size/2);
			px[y * tex_size + x] = 255 * clampf_n(1 - std::sqrt(xd*xd + yd*yd));
		}
		
		tex.set(GL_R8, {tex_size, height}, 0, 1, px.data(), GL_RED);
	}
	void gen_wall_mask(const LevelTerrain& lt)
	{
		std::vector<uint8_t> px;
		px.reserve(lt.grid_size.area());
		for (auto& c : lt.cs) px.push_back(c.is_wall? 0 : 255);
		mask.set(GL_R8, lt.grid_size, 0, 1, px.data(), GL_RED);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		mask_size = lt.grid_size * GameConst::cell_size;
	}
	int add_light()
	{
		std::unique_lock lock(lights_lock);
		int i = lights.emplace_new();
		lights[i].off = 0;
		do_update = true;
		return i;
	}
	void rem_light(int i)
	{
		std::unique_lock lock(lights_lock);
		lights.free_and_reset(i);
		do_update = true;
	}
	void set_light(int i, vec2fp ctr, float radius, float angle)
	{
		std::unique_lock lock(lights_lock);
		auto& lt = lights[i];
		do_update = true;
		
		vec2fp fw = vec2fp(radius, 0).fastrotate(angle + M_PI);
		vec2fp sd = vec2fp(fw).rot90cw();
		ctr -= fw * (float(tex_fade) / tex_size);
		
		lt.ps[0] = ctr - sd;
		lt.ps[1] = ctr + sd;
		lt.ps[2] = ctr - sd + fw;
		lt.ps[3] = ctr + sd + fw;
	}
	void set_light(int i, FColor clr)
	{
		std::unique_lock lock(lights_lock);
		auto& lt = lights[i];
		do_update = true;
		lt.clr = clr;
		lt.clr.a *= clr_mul;
	}
	void render()
	{
		std::unique_lock lock(lights_lock);
		if (!lights.existing_count() || !sh->is_ok() || !sh_mask->is_ok() || !enabled)
			return;
		
		if (do_update)
		{
			do_update = false;
			vao_data.reserve(lights.existing_count() * 6*4);
			vao_data.clear();
			
			vec2fp ts[4] = {{0,0}, {1,0}, {0,1}, {1,1}};
			for (auto& lt : lights) {
				lt.off = vao_data.size() /4;
				auto push = [&](int i){
					vao_data.emplace_back(lt.ps[i].x);
					vao_data.emplace_back(lt.ps[i].y);
					vao_data.emplace_back(ts[i].x);
					vao_data.emplace_back(ts[i].y);
				};
				// first triangle (11 - 21 - 12)
				push(0);
				push(1);
				push(2);
				// second triangle (21 - 12 - 22)
				push(1);
				push(2);
				push(3);
			}
			
			vao.bufs[0]->update(vao_data);
		}
		
		// draw mask
		
		glEnable(GL_STENCIL_TEST);
		glClear(GL_STENCIL_BUFFER_BIT);
		
		glStencilFunc(GL_ALWAYS, 1, 1);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		
		glActiveTexture(GL_TEXTURE0);
		mask.bind();
		
		sh_mask->bind();
		sh_mask->set4mx("proj", RenderControl::get().get_world_camera().get_full_matrix());
		sh_mask->set2f("sz", mask_size);
		
		RenderControl::get().ndc_screen2().bind();
		glDrawArrays(GL_TRIANGLES, 0, 6);
		
		// draw lights
		
		glStencilFunc(GL_EQUAL, 1, 1);
		glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
		
		tex.bind();
		sh->bind();
		sh->set4mx("proj", RenderControl::get().get_world_camera().get_full_matrix());
		vao.bind();
		
		for (auto& lt : lights) {
			sh->set_clr("clr", lt.clr);
			glDrawArrays(GL_TRIANGLES, lt.off, 6);
		}
		
		glDisable(GL_STENCIL_TEST);
	}
};



RenLightRef::RenLightRef(RenLightRef&& r) noexcept
{
	i = r.i;
	r.i = -1;
}
RenLightRef& RenLightRef::operator=(RenLightRef&& r) noexcept
{
	if (i != -1) static_cast<RenLight_Impl&>(RenLight::get()).rem_light(i);
	i = r.i;
	r.i = -1;
	return *this;
}
RenLightRef::~RenLightRef()
{
	if (i != -1) static_cast<RenLight_Impl&>(RenLight::get()).rem_light(i);
}
void RenLightRef::set_type(vec2fp ctr, float radius, float angle)
{
	if (i == -1) i = static_cast<RenLight_Impl&>(RenLight::get()).add_light();
	static_cast<RenLight_Impl&>(RenLight::get()).set_light(i, ctr, radius, angle);
}
void RenLightRef::set_color(FColor clr)
{
	if (i == -1) i = static_cast<RenLight_Impl&>(RenLight::get()).add_light();
	static_cast<RenLight_Impl&>(RenLight::get()).set_light(i, clr);
}



static RenLight_Impl* rni;
RenLight& RenLight::get() {
	if (!rni) LOG_THROW_X("RenLight::get() null");
	return *rni;
}
RenLight* RenLight::init() {return rni = new RenLight_Impl;}
RenLight::~RenLight() {rni = nullptr;}
