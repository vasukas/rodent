#ifndef PP_GRAPH_HPP
#define PP_GRAPH_HPP

#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_types.hpp"
#include "control.hpp"
#include "shader.hpp"

class PP_Filter;



class PP_Node
{
public:
	const std::string name;
	const bool needs_input;
	const bool needs_output;
	
	
	PP_Node(std::string name, bool needs_input = true, bool needs_output = true); ///< Adds self to PP_Graph
	
	/// Called once before each step. Returns true if node is enabled
	virtual bool prepare() = 0;
	
	/// Called once per step when all inputs are ready (only if enabled)
	virtual void proc(GLuint output_fbo) = 0;
	
	/// Returns input framebuffer (called once per input, but only before proc())
	virtual GLuint get_input_fbo() = 0;
	
protected:
	friend class PP_Graph_Impl;
	virtual ~PP_Node() = default;
};



class PP_Graph
{
public:
	static PP_Graph& get(); ///< Returns singleton
	
	/// Output order - lower drawn earlier amongst providers for same target
	virtual void connect(std::string output, std::string input, int order = 0) = 0;
	
protected:
	friend class Postproc_Impl;
	static PP_Graph* init();
	virtual ~PP_Graph();
	virtual void render() = 0;
	
	friend PP_Node;
	virtual void add_node(PP_Node* node) = 0;
};



/// Chain of fullscreen filters
class PPN_Chain : public PP_Node
{
public:
	std::function<bool()> is_enabled;
	
	// pre_draw is called before rendering to output buffer
	PPN_Chain(std::string name, std::vector<std::unique_ptr<PP_Filter>> fts_in, std::function<void()> pre_draw_in);
	
private:
	std::vector<std::unique_ptr<PP_Filter>> fts;
	std::function<void()> pre_draw;
	RAII_Guard rsz_g;
	
	GLA_Framebuffer fbo_s[2];
	GLA_Texture tex_s[2];
	
	bool clear_fbo = false;
	bool bs_index = false; ///< Index of FBO to which next draw will be
	bool bs_last; ///< Is last effect
	
	GLuint fbo_out;
	
	
	bool prepare() override;
	void proc(GLuint output_fbo) override;
	GLuint get_input_fbo() override;
	
	friend PP_Filter;
	void draw(bool is_last);
};



/// Wrapper for renderers
class PPN_InputDraw : public PP_Node
{
public:
	enum Middle_FBO {
		MID_NONE = 0,
		MID_COLOR = 1,
		MID_DEPTH_STENCIL = 2
	};
	
	bool enabled = true;
	PPN_InputDraw(std::string name, Middle_FBO mid_fbo, std::function<void(GLuint fbo)> func);
	
private:
	std::unique_ptr<Shader> pass;
	std::optional<GLA_Framebuffer> fbo;
	RAII_Guard fbo_g;
	std::function<void(GLuint)> func;
	bool alt_blend;
	
	bool prepare() override {return enabled;}
	void proc(GLuint fbo_out) override;
	GLuint get_input_fbo() override {throw std::logic_error("PPN_InputDraw::get_input_fbo() called");}
};



/// Wrapper for display FBO
class PPN_OutputScreen : public PP_Node
{
public:
	GLuint fbo = 0;
	PPN_OutputScreen() : PP_Node("display", true, false) {}
	
private:
	bool prepare() override {
		if (fbo) {
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glClearColor(0, 0, 0, 0);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		return true;
	}
	void proc(GLuint) override {}
	GLuint get_input_fbo() override {return fbo;}
};



/// Fullscreen shader effect
class PP_Filter
{
public:
	bool enabled = true;
	std::unique_ptr<Shader> sh;
	
	
	virtual ~PP_Filter() = default;
	virtual void proc() = 0;
	
	/// Is internal state ok
	bool is_ok() {return sh && sh->is_ok() && is_ok_int();}
	virtual bool is_ok_int() {return true;}
	
protected:
	void draw(bool is_last) {_chain->draw(is_last);}
	TimeSpan get_passed() {return RenderControl::get().get_passed();}
	
private:
	friend PPN_Chain;
	PPN_Chain* _chain = nullptr;
};

#endif // PP_GRAPH_HPP
