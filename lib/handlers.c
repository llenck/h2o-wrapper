#include "h2ow/handlers.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <fnmatch.h>

#define streq(a, b) (strcmp(a, b) == 0)

void h2ow__init_handler_lists(h2ow_handler_lists* hl) {
	// init lengths to 0, and pointers to NULL for realloc
	memset(hl, 0, sizeof(*hl));
}

// TODO this one isn't used anywhere; is there a good place to call it from?
void h2ow__free_handler_lists(h2ow_handler_lists* hl) {
	// regexes need to be regfree'd first
	int num_regex_handlers = hl->num_handlers[H2OW_REGEX_PATH];

	for (int i = 0; i < num_regex_handlers; i++) {
		regfree(&hl->regexes[i]);
	}
	free(hl->regexes);

	// after that, free all handler lists normally
	for (int i = 0; i < H2OW_NUM_PATH_TYPES; i++) {
		free(hl->handlers_lists[i]);
	}
}

static int h2ow_register_handler6(h2ow_context* wctx, int methods, const char* path,
                                  int type, void* handler, int call_type) {
	// first, make pointers to the things we actually want to change
	h2ow_handler_lists* hl = &wctx->handlers;

	int* num_handlers = &(hl->num_handlers[type]);
	h2ow_request_handler** handlers = &(hl->handlers_lists[type]);

	// for regex handlers, precompile the regex
	// (this must be done before increasing *num_handlers, or
	// free_handler_lists, which is called on error, will attempt to free
	// an invalid regex)
	if (type == H2OW_REGEX_PATH) {
		regex_t* new_regexes
		        = realloc(hl->regexes, (*num_handlers + 1) * sizeof(*hl->regexes));

		if (new_regexes == NULL) {
			h2ow__free_handler_lists(hl);
			return 0;
		}

		hl->regexes = new_regexes;

		regex_t* current_regex = &hl->regexes[*num_handlers];

		if (regcomp(current_regex, path, REG_EXTENDED) != 0) {
			h2ow__free_handler_lists(hl);
			return 0;
		}
	}

	// increase the number of handlers by one, then try to realloc
	*num_handlers += 1;
	h2ow_request_handler* new_handlers
	        = realloc(*handlers, *num_handlers * sizeof(**handlers));

	if (new_handlers == NULL) {
		h2ow__free_handler_lists(hl);
		return 0;
	}
	// realloc worked, replace the old pointer
	*handlers = new_handlers;

	// fill in the info
	h2ow_request_handler* new_handler = &(*handlers)[*num_handlers - 1];
	new_handler->handler = handler;
	new_handler->methods = methods;
	new_handler->path = path; // maybe useful for debugging in case of REGEX_PATH
	new_handler->call_type = call_type;

	return 1;
}

int h2ow_register_handler(h2ow_context* wctx, int methods, const char* path, int type,
                          void (*handler)(h2o_req_t*, h2ow_run_context*)) {
	return h2ow_register_handler6(wctx, methods, path, type, (void*)handler,
	                              H2OW_HANDLER_NORMAL);
}

int h2ow_register_handler_co(h2ow_context* wctx, int methods, const char* path, int type,
                             void (*handler)(h2o_req_t*, h2ow_run_context*,
                                             h2ow_resume_args*)) {
	return h2ow_register_handler6(wctx, methods, path, type, (void*)handler,
	                              H2OW_HANDLER_CO);
}

// match_handlers doesn't actually take current_handler since
// it needs access to the whole handler_lists structure for
// REGEX_PATH matching
static inline int match_handler(const h2ow_handler_lists* hl, int type, int i,
                                const char* path, int method) {
	const h2ow_request_handler* handler = &hl->handlers_lists[type][i];
	if (!(handler->methods & method))
		return 0;

	switch (type) {
	case H2OW_FIXED_PATH:
		return streq(handler->path, path);
		break;

	case H2OW_WILDCARD_PATH:
		// FNM_PATHNAME makes "/test*" not match "/test/asd"
		return fnmatch(handler->path, path, FNM_PATHNAME) == 0;
		break;

	case H2OW_REGEX_PATH:
		return regexec(&hl->regexes[i], path, 0, NULL, 0) == 0;
		break;
	}
	return 0; // make gcc happy; we actually always return in the switch above
}

h2ow_request_handler* h2ow__find_matching_handler(h2ow_handler_lists* hl,
                                                  const char* path, int method) {
	// make an array to order the different types of handlers
	int type_order[H2OW_NUM_PATH_TYPES]
	        = { H2OW_FIXED_PATH, H2OW_WILDCARD_PATH, H2OW_REGEX_PATH };

	for (int i = 0; i < H2OW_NUM_PATH_TYPES; i++) {
		// first figure out which list we need
		int current_type = type_order[i];
		int num_handlers = hl->num_handlers[current_type];

		// then loop through handlers, returning if a match is found
		for (int j = 0; j < num_handlers; j++) {
			h2ow_request_handler* current_handler = &hl->handlers_lists[current_type][j];

			// see comment above match_handler on why this needs the whole
			// handler_lists structure instead of just the current request_handler
			if (match_handler(hl, current_type, j, path, method)) {
				return current_handler;
			}
		}
	}

	// no matching handler
	return NULL;
}
