#ifndef SHADER_HPP
#define SHADER_HPP

#include <string>
#include "vaslib/vas_math.hpp"
#include "gl_utils.hpp"



/// Shader program wrapper
class Shader
{
public:
	struct Define
	{
		std::string name;
		std::string value; ///< Call rebuild() to apply changes
		
		bool is_src = false; ///< Set to true if was defined in shader (not used internally)
		bool is_default = true; ///< Set to false if value shouldn't be replaced with default on reload
	};
	
	std::vector<Define> def_list; ///< Parameters, either specified by shader or added manually
	
	std::function<void(Shader&)> on_reb; ///< Called on rebuild, shader is already bound
	std::function<void(Shader&)> pre_link; ///< Called on rebuild between attaching shaders and linking
	
	
	
	/// Never returns null
	static Shader* load(const char *name, bool is_critical = false, bool do_build = true);
	
	/// Non-critical, sets on_reb and builds shader
	static Shader* load_cb(const char *name, std::function<void(Shader&)> on_reb);
	
	const std::string& get_name() const {return name;}
	bool is_ok() const {return prog;} ///< Returns true if can be used
	
	Define* get_def(std::string_view name); ///< Returns define from 'def_list' or null if not found
	GLuint get_prog() {return prog;} ///< May return 0
	
	bool rebuild(bool forced = true); ///< Just rebuilds program
	bool reload(); ///< Loads shader sources and rebuilds program if needed
	
	void bind(); ///< Builds shader if not already
	
	void set1i(const char *name, int v);
	void set1f(const char *name, float v);
	void set2f(const char *name, float a, float b);
	void set3f(const char *name, float x, float y, float z);
	void set4f(const char *name, float r, float g, float b, float a);
	void setfv(const char *name, const float *v, int n);
	void set2mx(const char *name, const float *v, bool do_transpose = false);
	void set3mx(const char *name, const float *v, bool do_transpose = false);
	void set4mx(const char *name, const float *v, bool do_transpose = false);
	void set_rgba(const char *name, uint32_t clr, float mul = 1.f);
	void set_clr(const char *name, const FColor& clr);
	void set2f(const char *name, const vec2fp& p);
	
	
	
private:
	friend class RenderControl_Impl;
	
	struct DEL {void operator()(Shader* p){delete p;}};
	static std::vector<std::unique_ptr<Shader, DEL>> sh_col;
	
	struct SingleShader
	{
		std::string name; ///< file name
		GLenum type;
		std::string full_name; ///< name with type
		std::string src_version; ///< #version
		std::string src_code;
		std::vector<Define> defs;
	};
	
	static std::shared_ptr<SingleShader> read_shader(std::vector<std::string_view>& lines, size_t& i, const std::string& full_name);
	static std::shared_ptr<SingleShader> get_shd(GLenum type, std::string name);
	
	GLuint prog = 0; ///< Program object (can be 0)
	std::vector<std::shared_ptr<SingleShader>> src; ///< Indices into 'sh_src'
	
	std::string name;
	bool validate = false;
	
	
	
	Shader() = default;
	~Shader() {reset_prog();}
	void reset_prog();
	
	//
	
	/// Returns binding point or -1 if not found
	GLint find_loc( const char *uniform_name );
	
	static void set1i(int loc, int v);
	static void set1f(int loc, float v);
	static void set2f(int loc, float a, float b);
	static void set3f(int loc, float x, float y, float z);
	static void set4f(int loc, float r, float g, float b, float a);
	static void setfv(int loc, const float *v, int n);
	static void set2mx(int loc, const float *v, bool do_transpose = false);
	static void set3mx(int loc, const float *v, bool do_transpose = false);
	static void set4mx(int loc, const float *v, bool do_transpose = false);
	static void set_rgba(int loc, uint32_t clr, float mul = 1.f);
	static void set_clr(int loc, const FColor& clr);
	static void set2f(int loc, const vec2fp& p);
};

#endif // SHADER_HPP
