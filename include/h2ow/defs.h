#ifndef _H2OW_DEFS_H_INCLUDED
#define _H2OW_DEFS_H_INCLUDED

#define H2O_USE_LIBUV 1

#include <sys/types.h>
#include <regex.h>
#include <h2o.h>
#include <uv.h>

// define likely() and unlikely() depending on the compiler
#if defined(__GNUC__) || defined(__clang__)
	#ifndef likely
		#define likely(x) __builtin_expect(!!(x), 1)
	#endif
	#ifndef unlikely
		#define unlikely(x) __builtin_expect((x), 0)
	#endif
#else
	#ifndef likely
		#define likely(x) (x)
	#endif
	#ifndef unlikely
		#define unlikely(x) (x)
	#endif
#endif

// some typedefs
typedef struct h2ow_settings_s h2ow_settings;
typedef struct h2ow_run_context_s h2ow_run_context;
typedef struct h2ow_context_s h2ow_context;
typedef struct h2ow_request_handler_s h2ow_request_handler;
typedef struct h2ow_handler_lists_s h2ow_handler_lists;

typedef struct h2ow_handler_and_data_s h2ow_handler_and_data;

/* ================ HANDLER STUFF ================ */
// save supported methods as a bit field
#define H2OW_METHOD_ANY (~0)
#define H2OW_METHOD_GET 0x01
#define H2OW_METHOD_POST 0x02
#define H2OW_METHOD_HEAD 0x04
#define H2OW_METHOD_PUT 0x08
#define H2OW_METHOD_DELETE 0x10
#define H2OW_METHOD_OPTIONS 0x20
#define H2OW_METHOD_CONNECT 0x40
#define H2OW_METHOD_PATCH 0x80
#define H2OW_METHOD_TRACE 0x100

// idk how enums work lol
// also, these need to be 0-(NUM_PATH_TYPES - 1) or stuff will break horribly
#define H2OW_NUM_PATH_TYPES 3
#define H2OW_FIXED_PATH 0
#define H2OW_WILDCARD_PATH 1
#define H2OW_REGEX_PATH 2

// regex_t's are stored in a seperate array instead of inside the request_handler
// because on my machine, they are 64 bytes long, while the pointer only uses 8 bytes.
// that way the handler_lists which aren't of type REGEX_PATH are way smaller,
// which should give less ram usage and better cache performance
struct h2ow_request_handler_s {
	void (*handler)(h2o_req_t*, h2ow_run_context*);
	int methods;
	const char* path;
};

struct h2ow_handler_lists_s {
	int num_handlers[H2OW_NUM_PATH_TYPES];
	h2ow_request_handler* handlers_lists[H2OW_NUM_PATH_TYPES];
	regex_t* regexes; // see comment above the h2ow_request_handler declaration
};

/* ================ SETTINGS STUFF ================ */

struct h2ow_settings_s {
	const char* ip;
	int port;
	int shutdown_timeout;
	const char* log_format;
	int thread_count;
	int debug_level;
};

/* ================ PRIVATE STUFF ================ */
// since h2o's way of passing around data is weird af, use this
// to pass the h2o_run_context to the request handler
struct h2ow_handler_and_data_s {
	h2o_handler_t super;
	void* more_data;
};

/* ================ USER-VISIBLE STUFF ================ */
// info about stuff running in the current thread
struct h2ow_run_context_s {
	h2ow_context* wctx;
	h2ow_handler_and_data* root_handler;

	h2o_globalconf_t globconf;
	h2o_hostconf_t* hostconf;
	h2o_context_t ctx;
	h2o_accept_ctx_t accept_ctx;

	uv_tcp_t listener;
	uv_loop_t loop;
	uv_signal_t int_handler, term_handler;

	int num_connections;

	// number of close callbacks currently running that should finish before
	// a close callback should use uv_stop();
	// this is only used in case of error, since we otherwise know how much is left
	int running_cleanup_cbs;
};

// for a set of threads
struct h2ow_context_s {
	// array of h2ow_run_contexts which are thread-local
	h2ow_run_context* run_contexts;
	pthread_t* threads;

	// settings, which are set before initializing anything from libuv or h2o
	h2ow_settings settings;

	// list of handlers
	h2ow_handler_lists handlers;

	// false during cleanup, true if currently accepting and working on connections
	int is_running;
};

#endif

