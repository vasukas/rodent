#ifndef DBGCONS_HPP
#define DBGCONS_HPP

#include <functional>

union SDL_Event;



class Console
{
public:
	enum AuthLevel
	{
		AUTH_USER,
		AUTH_ADMIN,
		AUTH_LOCAL
	};
	
	/// Command parameters
	struct Cmd
	{
		std::vector <std::string> args; ///< All quotes removed
		AuthLevel auth; ///< At which level executed
		
		// argument getters - convert next argument after in index if possible, otherwise return false
		
		Cmd( std::string_view s, AuthLevel auth );
		bool int_arg  ( size_t i, int&   n );
		bool float_arg( size_t i, float& n );
	};
	
	/// Return values for handler
	enum HandleRet
	{
		H_OK,  ///< Command was processed
		H_ERR, ///< Command is invalid
		H_UNK, ///< Command is not for this handler
		H_AUTH ///< Authorization level is too low
	};
	
	/// Command handler callback
	typedef std::function <int( Cmd& )> Handler;
	
	
	static const char *get_msg(HandleRet ret); ///< Returns message corresponding to code, never fails
	
	static Console& get(); ///< Returns singleton
	virtual ~Console() = default; ///< Clears internal data only
	
	
	/// Registers new callback, returns it's internal index
	virtual size_t reg( Handler f, const char *name, AuthLevel auth_minimal ) = 0;
	
	/// Unregisters handler by internal index
	virtual void unreg( size_t i ) = 0;
	
	/// Disables or enables handler
	virtual void pause( size_t i, bool on ) = 0;
	
	
	/// Just writes to output
	virtual void print( std::string_view str, bool newline = false ) = 0;
	
	/// Executes command, returns value from handler or HandleRet
	virtual bool exec( std::string_view str, AuthLevel auth ) = 0;
	
	
	virtual void on_event( SDL_Event& ev ) = 0;
	virtual void render() = 0;
};



class ConsoleHandler {
public:
	Console::Handler func;
	
	ConsoleHandler();
	ConsoleHandler(const char *name, Console::AuthLevel auth, Console::Handler h);
	ConsoleHandler(const ConsoleHandler&) = delete;
	ConsoleHandler(ConsoleHandler&& ch);
	ConsoleHandler& operator=(ConsoleHandler&& ch);
	~ConsoleHandler();
	
private:
	size_t i;
};

#endif // DBGCONS_HPP
