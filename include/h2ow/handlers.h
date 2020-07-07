#ifndef _H2OW_HANDLERS_H_INCLUDED
#define _H2OW_HANDLERS_H_INCLUDED

#include "defs.h"

#include <sys/types.h>
#include <regex.h>

// initialize length info and pointers for a handler list
void h2ow__init_handler_lists(h2ow_handler_lists* hl);
void h2ow__free_handler_lists(h2ow_handler_lists* hl);

int h2ow_register_handler(h2ow_context* wctx, int methods, const char *path, int type,
		void (*handler)(h2o_req_t*, h2ow_run_context*));

// try to find a matching handler, given a path and method
// return either a pointer to the handler or NULL on failure
h2ow_request_handler* h2ow__find_matching_handler(h2ow_handler_lists* hl,
		const char* path, int method);

#endif

