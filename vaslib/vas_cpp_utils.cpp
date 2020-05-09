#include "vas_cpp_utils.hpp"

#ifdef __linux__
#include <pthread.h>
void set_this_thread_name(const char *name) {
	pthread_setname_np(pthread_self(), name);	
}
#else
void set_this_thread_name(const char *)
{}
#endif
