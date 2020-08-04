#ifndef _H2OW_RUNTIME_H_INCLUDED
#define _H2OW_RUNTIME_H_INCLUDED

#include "defs.h"

// subclass of uv_tcp_t used to pass data in cases where h2o internally already
// uses the data field
typedef struct uv_tcp_and_data_s {
	uv_tcp_t super;
	void* more_data; // not named data to differentiate more from super.data
} uv_tcp_and_data;

// subclass for h2ow_handler_t used to pass data (for our purposes, a h2ow_run_context*)
typedef struct h2o_handler_and_data_s {
	h2o_handler_t super;
	void* more_data; // could be named data, but lets be consistent with uv_tcp_and_data_s
} h2o_handler_and_data;

/* these next two functions can be used to close and optionally free handles
 * from a libuv loop, and then possibly stop the loop.
 *
 * both functions except handle->data to be a pointer to an h2ow_run_context whose
 * running_cleanup_cbs field they decrement. if they decremented that count to 0,
 * these functions stop the loop.
 */
void h2ow__cleanup_cb(uv_handle_t* self);
void h2ow__cleanup_free_cb(uv_handle_t* self);

// callbacks for the event loop
void h2ow__on_signal(uv_signal_t* self, int signum);
void h2ow__on_close(uv_handle_t* conn);
void h2ow__on_accept(uv_stream_t* listener, int status);

// handler that passes the request on the the user-defined handlers.
// needed because h2o's handler system only handles prefixes, while we allow for
// wildcard and regex handlers
int h2ow__request_handler(h2o_handler_t* self, h2o_req_t* req);

#endif
