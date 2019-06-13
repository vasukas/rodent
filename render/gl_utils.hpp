#ifndef GL_UTILS_HPP
#define GL_UTILS_HPP

#include <vector>
#include <GL/glew.h>



/// Returns byte size for GL types enumerations (GL_FLOAT etc)
size_t gl_type_size( GLenum type_enum );



/// Wrapper for vertex array object with single buffer
struct GLA_SingleVAO
{
	GLuint vao = 0;
	GLuint vbo = 0;
	
	
	GLA_SingleVAO(); ///< Creates object
	~GLA_SingleVAO(); ///< Destroys object
	
	GLA_SingleVAO( const GLA_SingleVAO& ) = delete;
	void operator =( const GLA_SingleVAO& ) = delete;
	
	GLA_SingleVAO( GLA_SingleVAO&& );
	void operator =( GLA_SingleVAO&& );
	
	
	/// Binds array object
	void bind();
	
	/// Binds buffer object
	void bind_buf( GLenum target = GL_ARRAY_BUFFER );
	
	/// Sets attribute; binds both VAO and VBO
	void set_attrib( size_t location, size_t el_count, GLenum el_type = GL_FLOAT, size_t stride = 0, size_t offset = 0 );
	
	struct Attrib {
		size_t el_count;
		GLenum el_type;
	};
	/// Automatically sets all attributes with calculated strides and offsets; binds both VAO and VBO
	void set_attribs( const std::vector< Attrib >& attrs );
};

#endif // GL_UTILS_HPP
