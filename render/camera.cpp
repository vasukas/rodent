#include "camera.hpp"
#include "control.hpp"

void Camera::set_state (Frame frm)
{
	cur = frm;
	mx_req_upd = 3;
}
Camera::Frame& Camera::mut_state()
{
	mx_req_upd = 3;
	return cur;
}
void Camera::set_vport (Rect vp)
{
	vport = vp;
	mx_req_upd = 3;
}
void Camera::set_vport_full()
{
	vport.size( RenderControl::get_size() );
	mx_req_upd = 3;
}



static void mx_mul(float *m, float *s) {
	float t[16] = {};
	for (int i=0; i<4; i++) {
		for (int j=0; j<4; j++) {
			for (int k=0; k<4; k++)
				t[i*4+j] += m[i*4+k] * s[k*4+j];
		}
	}
	for (int i=0; i<16; i++) m[i] = t[i];
}
static void mx_mul_vec(const float *m, float *v) {
	float t[4] = {};
	for (int i=0; i<4; i++) {
		for (int k=0; k<4; k++)
			t[i] += m[i*4+k] * v[k];
	}
	for (int i=0; i<4; i++) v[i] = t[i];
}
static void mx_one(float *m) {
	for (int i=0; i<16; i++) m[i] = i%5? 0 : 1;
}
static void mx_scale(float *m, float x, float y) {
	mx_one(m);
	m[0] = x;
	m[5] = y;
	m[10] = 0.f;
}
static void mx_translate(float *m, float x, float y) {
	mx_one(m);
	m[3] = x;
	m[7] = y;
	m[11] = 0.f;
}
static void mx_rotate(float *m, float rot) {
	mx_one(m);
	float c = cosf(rot);
	float s = sinf(rot);
	m[0] = c; m[1] = -s;
	m[4] = s; m[5] = c;
}
static void mx_transpose(float *m)
{
	std::swap(m[1], m[4]);
	std::swap(m[2], m[8]);
	std::swap(m[3], m[12]);
	std::swap(m[6], m[9]);
	std::swap(m[7], m[13]);
	std::swap(m[11], m[14]);
}
const float* Camera::get_full_matrix() const
{
	/*
		2D camera transform
		we need to:
		- translate to camera position
		- translate to screen origin
		- rotate world
		- zoom and scale to NDC
		so all these transforms are applied in inverse order
	*/
	
	if (mx_req_upd & 1)
	{
		mx_req_upd &= 2;
		
		float *mx = mx_full;
		vec2i sz = vport.size();
		int w = sz.x, h = sz.y;
		
		vec2fp pos = cur.pos;
		float rot = cur.rot;
		float mg_x = cur.mag;
		float mg_y = cur.mag;
		
		float t[16];
		mx_one(mx);
		mx_scale(t, mg_x / (w/2), -mg_y / (h/2));    mx_mul(mx, t);
		mx_rotate(t, -rot);                          mx_mul(mx, t);
		mx_translate(t, -pos.x, -pos.y);             mx_mul(mx, t);
		mx_transpose(mx);
	}
	
	return mx_full;
}
vec2fp Camera::mouse_cast(vec2i mou) const
{
	float *m = mx_rev;
	vec2i sz = vport.size();
	int w = sz.x, h = sz.y;
	
	if (mx_req_upd & 2)
	{
		mx_req_upd &= 1;
		
		vec2fp pos = cur.pos;
		float rot = cur.rot;
		float mg_x = cur.mag;
		float mg_y = cur.mag;
		
		float t[16];
		mx_one(m);
		mx_translate(t, pos.x, pos.y);     mx_mul(m, t);
		mx_rotate(t, rot);                 mx_mul(m, t);
		mx_scale(t, 1. / mg_x, 1. / mg_y); mx_mul(m, t);
	}
	
	float v[4] = {(float) mou.x - w/2, (float) mou.y - h/2, 0, 1};
	mx_mul_vec(m, v);
	return {v[0], v[1]};
}
vec2i Camera::direct_cast( vec2fp p ) const
{
	auto m = get_full_matrix();
	vec2i sz = vport.size();
	int w = sz.x, h = sz.y;
	
	p -= cur.pos;
	p.y = -p.y;
	
	float v[4] = {p.x, p.y, 0, 1};
	mx_mul_vec(m, v);
	return vec2i(std::roundf((v[0] + 1)*(w/2)),
	             std::roundf((v[1] + 1)*(h/2)));
}
vec2fp Camera::coord_size() const
{
	return vport.size() / cur.mag;
}
