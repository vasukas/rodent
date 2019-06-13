#include "vaslib/vas_log.hpp"
#include "gl_utils.hpp"



size_t gl_type_size( GLenum t )
{
	switch( t )
	{
	case GL_FLOAT:      return 4;
	case GL_DOUBLE:     return 8;
		
	case GL_HALF_FLOAT: return 2;
	case GL_FIXED:      return 4;
		
	case GL_UNSIGNED_BYTE:
	case GL_BYTE:
		return 1;
		
	case GL_UNSIGNED_SHORT:
	case GL_SHORT:
		return 2;
		
	case GL_UNSIGNED_INT:
	case GL_INT:
		return 4;
	}
	
	ASSERT( false, "gl_type_size() invalid enum value" );
	return 0;
}



GLA_SingleVAO::GLA_SingleVAO()
{
	glGenVertexArrays( 1, &vao );
	glGenBuffers( 1, &vbo );
}
GLA_SingleVAO::~GLA_SingleVAO()
{
	glDeleteVertexArrays( 1, &vao );
	glDeleteBuffers( 1, &vbo );
}
GLA_SingleVAO::GLA_SingleVAO( GLA_SingleVAO&& obj )
{
	std::swap( vao, obj.vao );
	std::swap( vbo, obj.vbo );
}
void GLA_SingleVAO::operator =( GLA_SingleVAO&& obj )
{
	std::swap( vao, obj.vao );
	std::swap( vbo, obj.vbo );
}
void GLA_SingleVAO::bind()
{
	glBindVertexArray( vao );
}
void GLA_SingleVAO::bind_buf( GLenum target ) {
	glBindBuffer( target, vbo );
}
void GLA_SingleVAO::set_attrib( size_t index, size_t el_count, GLenum el_type, size_t stride, size_t offset )
{
	glBindVertexArray( vao );
	glBindBuffer( GL_ARRAY_BUFFER, vbo );
	glEnableVertexAttribArray( index );
	glVertexAttribPointer( index, el_count, el_type, GL_FALSE, stride, reinterpret_cast< void* >(offset) );
}
void GLA_SingleVAO::set_attribs( const std::vector< Attrib >& attrs )
{
	size_t stride = 0;
	for (auto &a : attrs)
		stride += a.el_count * gl_type_size( a.el_type );
	
	size_t i = 0, offset = 0;
	for (auto &a : attrs)
	{
		set_attrib( i, a.el_count, a.el_type, stride, offset );
		offset += a.el_count * gl_type_size( a.el_type );
		++i;
	}
}
