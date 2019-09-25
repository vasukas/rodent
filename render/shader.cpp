#include "shader.hpp"
#include "vaslib/vas_file.hpp"
#include "vaslib/vas_log.hpp"



std::string Shader::load_path = "res/shaders/";

GLuint Shader::compile( GLenum type, std::string_view source )
{
	const char *str = source.data();
	GLint len = source.length();
	
	GLuint sh = glCreateShader(type);
	glShaderSource(sh, 1, &str, &len);
	glCompileShader(sh);
	
	GLint err = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &err);
	
	if (err == GL_FALSE)
	{
		GLint str_n = 0;
		glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &str_n);
		char *str = new char [str_n];
		GLsizei len = 0;
		glGetShaderInfoLog(sh, str_n, &len, str);
		VLOGE("Shader::compile():\n{}", std::string_view(str, len));
		delete[] str;
		
		glDeleteShader(sh);
		return 0;
	}
	
	if (log_test_level(LogLevel::Verbose))
	{
		GLint str_n = 0;
		glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &str_n);
		if (str_n)
		{
			char *str = new char [str_n];
			GLsizei len = 0;
			glGetShaderInfoLog(sh, str_n, &len, str);
			VLOGV("Shader:: compilation info:\n{}\nEND\n", std::string_view(str, len));
			delete[] str;
		}
	}
	return sh;
}
GLuint Shader::link(const std::vector <GLuint> &shaders)
{
	GLuint prog = glCreateProgram();
	for (auto &sh : shaders)
	{
		if (!sh) return 0;
		glAttachShader(prog, sh);
	}
	prog = link_fin(prog);
	if (prog) for (auto &sh : shaders) glDetachShader(prog, sh);
	return prog;
}
GLuint Shader::link_fin(GLuint prog)
{
	glLinkProgram(prog);
	
	GLint err;
	glGetProgramiv(prog, GL_LINK_STATUS, &err);
	
	if (err == GL_FALSE)
	{
		GLint str_n = 0;
		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &str_n);
		char *str = new char [str_n];
		GLsizei len = 0;
		glGetProgramInfoLog(prog, str_n, &len, str);
		VLOGE("Shader::link():\n{}", std::string_view(str, len));
		delete[] str;
		
		glDeleteProgram(prog);
		return 0;
	}
	
	if (log_test_level(LogLevel::Verbose))
	{
		GLint str_n = 0;
		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &str_n);
		if (str_n)
		{
			char *str = new char [str_n];
			GLsizei len = 0;
			glGetProgramInfoLog(prog, str_n, &len, str);
			VLOGV("Shader:: link info:\n{}\nEND\n", std::string_view(str, len));
			delete[] str;
		}
	}
	
	return prog;
}
Shader* Shader::make(const char *dbg_name, const std::vector <std::pair<GLenum, std::string_view>> &shaders)
{
	std::vector <GLuint> shs;
	shs.reserve( shaders.size() );
	
	for (auto &si : shaders)
	{
		GLuint sh = compile(si.first, si.second);
		if (!sh)
		{
			VLOGE("Shader::make() failed: {}", dbg_name);
			for (auto &s : shs) glDeleteShader(s);
			return nullptr;
		}
		shs.push_back(sh);
	}
	
	GLuint prog = link(shs);
	for (auto &s : shs) glDeleteShader(s);
	if (!prog)
	{
		VLOGE("Shader::make() failed: {}", dbg_name);
		return nullptr;
	}
	
	std::string type_dbg;
	type_dbg.reserve(shaders.size());
	for (auto& s : shaders) {
		switch (s.first) {
		case GL_VERTEX_SHADER:   type_dbg.push_back('V'); break;
		case GL_FRAGMENT_SHADER: type_dbg.push_back('F'); break;
		case GL_GEOMETRY_SHADER: type_dbg.push_back('G'); break;
		default: type_dbg.push_back('-'); break;
		}
	}
	VLOGD("Compiled shader program: {} [{}]", dbg_name, type_dbg);
	
	Shader* s = new Shader (prog);
	s->dbg_name = dbg_name;
	return s;
}
Shader* Shader::load(const char* filename)
{
	std::string fn;
	fn += load_path;
	fn += filename;
	
	auto file_opt = readfile( fn.data() );
	if (!file_opt)
	{
		VLOGE("Shader::load() failed");
		return nullptr;
	}
	std::string& file = *file_opt;
	
	const std::vector <std::pair<std::string, GLenum>> ps =
	{
	    {"#vert\n", GL_VERTEX_SHADER},
	    {"#frag\n", GL_FRAGMENT_SHADER},
	    {"#geom\n", GL_GEOMETRY_SHADER}
	};
	
	static const char *vert_pass_src = 
R"(#version 330 core

layout(location = 0) in vec2 vert;
out vec2 tc;

void main() {
	tc = vert * 0.5 + 0.5;
	gl_Position = vec4(vert, 0, 1);
})";
	
	static const char *vert_texel_src = 
R"(#version 330 core

uniform vec2 scr_px_size;
layout(location = 0) in vec2 vert;
out vec2 tc;

void main() {
	tc = scr_px_size * (vert * 0.5 + 0.5);
	gl_Position = vec4(vert, 0, 1);
})";
	
	std::vector <std::pair<GLenum, std::string_view>> shs;
	for (auto &p : ps)
	{
		size_t pos;
		while (true)
		{
			pos = file.find( p.first );
			if (pos == std::string::npos) break;
			if (pos != 0 && file[pos - 1] != '\n') continue;
			
			pos += p.first.length();
			break;
		}
		if (pos == std::string::npos) continue;
		
		size_t end = file.find( "#end", pos );
		if (end == std::string::npos)
		{
			VLOGE("Shader::load() no matchind #end for {}: \"{}\"", p.first, filename);
			return nullptr;
		}
		
		shs.emplace_back( p.second, std::string_view(file).substr(pos, end - pos) );
	}
	
	const std::vector <std::pair<std::string, std::pair<GLenum, std::string_view>>> marks =
	{
		{"#vert_pass\n", {GL_VERTEX_SHADER, vert_pass_src}},
	    {"#vert_texel\n", {GL_VERTEX_SHADER, vert_texel_src}}
	};
	for (auto& p : marks)
	{
		size_t pos;
		while (true)
		{
			pos = file.find( p.first );
			if (pos == std::string::npos) break;
			if (pos != 0 && file[pos - 1] != '\n') continue;
			
			pos += p.first.length();
			break;
		}
		if (pos != std::string::npos)
			shs.emplace_back( p.second.first, p.second.second );
	}
	
	std::string typestr;
	for (auto& s : shs) {
		switch (s.first)
		{
		case GL_VERTEX_SHADER:   typestr += 'v'; break;
		case GL_FRAGMENT_SHADER: typestr += 'f'; break;
		case GL_GEOMETRY_SHADER: typestr += 'g'; break;
		default:                 typestr += '?'; break;
		}
	}
	
	Shader* s = make(filename, shs);
	if (!s)
	{
		VLOGE("Shader::load() failed");
		return nullptr;
	}
	
	VLOGI("Loaded shader \"{}\" as {} [{}]", filename, s->prog, typestr);
	return s;
}



Shader::Shader( GLuint prog ) : prog (prog) {}
Shader::~Shader()
{
	glDeleteProgram(prog);
}
void Shader::operator =( Shader&& sh )
{
	std::swap( prog, sh.prog );
	validate = true;
}
GLuint Shader::get_obj()
{
	return prog;
}
bool Shader::do_validate()
{
	glValidateProgram(prog);
	
	GLint err;
	glGetProgramiv(prog, GL_VALIDATE_STATUS, &err);
	if (err == GL_FALSE)
		VLOGW("Validation failed: [{}] {}", prog, dbg_name);
	else
	{
		VLOGV("Validation ok: [{}] {}", prog, dbg_name);
		return true;
	}
	
	GLint str_n = 0;
	glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &str_n);
	if (str_n)
	{
		char *str = new char [str_n];
		GLsizei len = 0;
		glGetProgramInfoLog(prog, str_n, &len, str);
		VLOGW("Shader:: validation:\n{}\nEND\n", std::string_view(str, len));
		delete[] str;
	}
	return false;
}
void Shader::bind()
{
	if (validate)
	{
		validate = false;
		do_validate();
	}
	glUseProgram(prog);
}
GLint Shader::find_loc( const char *name )
{
	return glGetUniformLocation(prog, name);
}



void Shader::set1i(int loc, int v) {
	glUniform1i(loc, v);
}
void Shader::set1f(int loc, float v) {
	glUniform1f(loc, v);
}
void Shader::set2f(int loc, float a, float b) {
	glUniform2f(loc, a, b);
}
void Shader::set3f(int loc, float x, float y, float z) {
	glUniform3f(loc, x, y, z);
}
void Shader::set4f(int loc, float r, float g, float b, float a) {
	glUniform4f(loc, r, g, b, a);
}
void Shader::setfv(int loc, const float *v, int n) {
	glUniform1fv(loc, n, v);
}
void Shader::set2mx(int loc, const float *v, bool transp) {
	glUniformMatrix2fv(loc, 1, transp, v);
}
void Shader::set3mx(int loc, const float *v, bool transp) {
	glUniformMatrix3fv(loc, 1, transp, v);
}
void Shader::set4mx(int loc, const float *v, bool transp) {
	glUniformMatrix4fv(loc, 1, transp, v);
}
void Shader::set_rgba(int loc, uint32_t clr, float mul) {
	mul /= 255.f;
	glUniform4f(loc,
	      mul * (clr >> 24),
	      mul * ((clr >> 16) & 0xff),
	      mul * ((clr >> 8) & 0xff),
	      (clr & 0xff) / 255.f);
}
void Shader::set_clr(int loc, const FColor& clr) {
	glUniform4f(loc, clr.r, clr.g, clr.b, clr.a);
}



void Shader::set1i(const char *name, int v) {
	set1i( find_loc(name), v );
}
void Shader::set1f(const char *name, float v) {
	set1f( find_loc(name), v );
}
void Shader::set2f(const char *name, float a, float b) {
	set2f( find_loc(name), a, b );
}
void Shader::set3f(const char *name, float x, float y, float z) {
	set3f( find_loc(name), x, y, z );
}
void Shader::set4f(const char *name, float r, float g, float b, float a) {
	set4f( find_loc(name), r, g, b, a );
}
void Shader::setfv(const char *name, const float *v, int n) {
	setfv( find_loc(name), v, n );
}
void Shader::set2mx(const char *name, const float *v, bool transp) {
	set2mx( find_loc(name), v, transp );
}
void Shader::set3mx(const char *name, const float *v, bool transp) {
	set3mx( find_loc(name), v, transp );
}
void Shader::set4mx(const char *name, const float *v, bool transp) {
	set4mx( find_loc(name), v, transp );
}
void Shader::set_rgba(const char *name, uint32_t clr, float mul) {
	set_rgba( find_loc(name), clr, mul );
}
void Shader::set_clr(const char *name, const FColor& clr) {
	set_clr( find_loc(name), clr );
}
