#ifndef BLOCK_CFG_HPP
#define BLOCK_CFG_HPP

/*
	Simple block parser for configs.
	
	All lines within block has bigger indentation, both spaces and tabs can be used.
	Line consists of space-separated arguments, and can be a header to a block.
	
	Single-line comment starts with '#' and continues till end of line. 
	Multi-line comment starts with '#{' and ends with '}#', which can be nested, BUT!!!
		line containing closing token will be commented out till end of line!
	Argument can be quoted (quotes can be escaped) to ignore spaces and '#'.
	
	Empty and commented lines are removed from the output.
*/

#include <functional>
#include <string>
#include <vector>

struct BC_Line;



struct BC_Block
{
	std::vector <BC_Line> ls; ///< Can be empty
	int tab_level = 0; ///< For lines inside block
};

struct BC_Line
{
	std::vector <std::string> args; ///< Always contains at least 1 item
	size_t line; ///< Line in string, starting from 1
	BC_Block block; ///< To which this line is a header
};



/// Parses string into block, adding lines to it
bool bc_parse( BC_Block& top, std::string_view str, int spaces_per_tab = 2 );

/// Recursively dumps block to string. Set 'spaces_per_tab' to 0 to use tabs instead. 
/// Returns last line position
size_t bc_dump( const BC_Block& top, std::string& str, int spaces_per_tab = 2, size_t first_line = 1 );



struct BC_Cmd
{
	/// Callback called when such line is encountered after processing arguments (may be null). 
	/// If single is true, only one such command allowed per block
	BC_Cmd( bool optional, bool single, std::string name, std::function <bool()> cb );
	
	/// Adds new command to block
	void add( BC_Cmd c );
	
	// Arguments are parsed in same order as they were added. 
	// If optional and doesn't exists, value left unchanged (exception - arg_fixed). 
	// Fixed optional skips next few arguments if not encountered.
	
	void arg( std::string name ); ///< Non-optional fixed
	void arg( std::string name, size_t val_count ); ///< Optional fixed
	void arg( std::string name, size_t val_count, bool& exists ); ///< Optional fixed
	
	void val( int& value );
	void val( std::string& value );
	
private:
	struct Arg
	{
		std::string pref;
		union { int* vi; std::string* vs; bool* vb; };
		int type;
		size_t skip = 0;
	};
	
	bool opt, single;
	std::string name;
	std::function <bool()> cb;
	std::vector <BC_Cmd> cmds;
	std::vector <Arg> args;
	
	bool already = false;
	
	friend bool bc_process( const BC_Block&, std::vector <BC_Cmd> );
};

/// Processes block using commands
bool bc_process( const BC_Block& top, std::vector <BC_Cmd> cmds );



/// Loads, parses and processes file
bool bc_parsefile( const char *filename, std::vector <BC_Cmd> cmds, int spaces_per_tab = 2 );

/// Saves block to file
bool bc_dumpfile( const char *filename, const BC_Block& top, int spaces_per_tab = 2 );

#endif // BLOCK_CFG_HPP
