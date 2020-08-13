#ifndef _H2OW_DEFS_H_INCLUDED
#define _H2OW_DEFS_H_INCLUDED

#define H2O_USE_LIBUV 1

#include <sys/types.h>
#include <regex.h>
#include <h2o.h>
#include <uv.h>
#include <unico.h>

// define likely() and unlikely() depending on the compiler
#if defined(__GNUC__) || defined(__clang__)
#	ifndef likely
#		define likely(x) __builtin_expect(!!(x), 1)
#	endif
#	ifndef unlikely
#		define unlikely(x) __builtin_expect((x), 0)
#	endif
#else
#	ifndef likely
#		define likely(x) (x)
#	endif
#	ifndef unlikely
#		define unlikely(x) (x)
#	endif
#endif

// some typedefs
typedef struct h2ow_settings_s h2ow_settings;
typedef struct h2ow_run_context_s h2ow_run_context;
typedef struct h2ow_context_s h2ow_context;
typedef struct h2ow_request_handler_s h2ow_request_handler;
typedef struct h2ow_handler_lists_s h2ow_handler_lists;

typedef struct h2ow_handler_and_data_s h2ow_handler_and_data;
typedef struct h2ow_co_and_stack_s h2ow_co_and_stack;
typedef struct h2ow_co_params_s h2ow_co_params;

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

enum handler_type { H2OW_HANDLER_NORMAL, H2OW_HANDLER_CO };

// regex_t's are stored in a seperate array instead of inside the request_handler
// because on my machine, they are 64 bytes long, while the pointer only uses 8 bytes.
// that way the handler_lists which aren't of type REGEX_PATH are way smaller,
// which should give less ram usage and better cache performance
struct h2ow_request_handler_s {
	union {
		void* handler;
		void (*sync_handler)(h2o_req_t*, h2ow_run_context*);
		void (*co_handler)(h2o_req_t*, h2ow_run_context*, unico_co_state*);
	};

	const char* path;
	int methods;
	int call_type;
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

	const char* ssl_cert_path;
	const char* ssl_key_path;
	SSL_CTX* ssl_ctx;
	int ssl_port;
};

/* ================ PRIVATE STUFF ================ */
// since h2o's way of passing around data is weird af, use this
// to pass the h2o_run_context to the request handler
struct h2ow_handler_and_data_s {
	h2o_handler_t super;
	void* more_data;
};

// convenience struct for pool of stacks and coroutine objects
struct h2ow_co_and_stack_s {
	unico_stack stack;
	unico_co_state co;

	// since we want to be able to realloc() a buffer of these, store the index of
	// adjacent nodes instead of a pointer to them, and -1 instead of NULL
	h2ow_co_and_stack** pool;
	int prev, next;
};

// struct for passing data to coroutines in co->data
struct h2ow_co_params_s {
	h2o_req_t* req;
	h2ow_run_context* rctx;
	void (*handler)(h2o_req_t*, h2ow_run_context*, unico_co_state*);
};

/* ================ USER-VISIBLE STUFF ================ */
// info about stuff running in the current thread
struct h2ow_run_context_s {
	h2ow_context* wctx;
	h2ow_handler_and_data* root_handler;

	h2o_globalconf_t globconf;
	h2o_hostconf_t* hostconf;
	h2o_context_t ctx;
	h2o_accept_ctx_t accept_ctxs[2];

	uv_tcp_t listeners[2];
	uv_loop_t loop;
	uv_signal_t int_handler, term_handler;

	int num_connections;

	// number of close callbacks currently running that should finish before
	// a close callback should use uv_stop();
	// this is only used in case of error, since we otherwise know how much is left
	int running_cleanup_cbs;

	// we keep a pool of stacks and coroutine objects for calling coroutines in order to
	// minimize construction and destruction of those objects, which often involve
	// malloc and free calls
	h2ow_co_and_stack* co_pool;
	int co_pool_len;
	int co_pool_usage;
	// all coroutine objects are in a linked list; rh ("read head") is the pointer to
	// the start of active coroutines, wh ("write head") is the pointer to the first
	// unused h2ow_co_and_stack (or -1 if none) and ah ("append head") is the pointer
	// to the end of the linked list, where h2ow_co_and_stacks are recycled to
	int co_rh, co_wh, co_ah;
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

	// ssl context, which is shared between threads
	SSL_CTX* ssl_ctx;
};

#endif
