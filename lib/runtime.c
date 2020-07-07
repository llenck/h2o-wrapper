#include "h2ow/runtime.h"
#include "h2ow/settings.h"
#include "h2ow/handlers.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <h2o.h>
#include <uv.h>

/* these next two functions can be used to close and optionally free handles
 * from a libuv loop, and then possibly stop the loop.
 *
 * both functions except handle->data to be a pointer to an h2ow_run_context whose
 * running_cleanup_cbs field they decrement. if they decremented that count to 0,
 * these functions stop the loop.
 */
void h2ow__cleanup_cb(uv_handle_t* self) {
	h2ow_run_context* rctx = self->data;
	rctx->running_cleanup_cbs--;
	if (rctx->running_cleanup_cbs == 0) {
		uv_stop(&rctx->loop);
	}
}

void h2ow__cleanup_free_cb(uv_handle_t* self) {
	h2ow__cleanup_cb(self);

	free(self);
}

static void stop_loop(uv_timer_t* timer) {
	h2ow_run_context* rctx = timer->data;
	h2ow_settings* settings = &rctx->wctx->settings;

	H2OW_NOTE("Timer expired, stopping the loop (after cleaning up self)\n");

	uv_timer_stop(timer);

	rctx->running_cleanup_cbs = 1;
	uv_close((uv_handle_t*)timer, h2ow__cleanup_free_cb);
}

void h2ow__on_signal(uv_signal_t* self, int signum) {
	h2ow_run_context* rctx = self->data;
	h2ow_settings* settings = &rctx->wctx->settings;
	if (!rctx->wctx->is_running) {
		return;
	}

	switch (signum) {
	case SIGINT:
	case SIGTERM:
		H2OW_NOTE("Received termination signal, deleting listener and signal handlers\n");
		H2OW_NOTE("(Delivery of another signal will now instantly kill the process)\n");

		if (&rctx->int_handler != NULL)
			uv_close((uv_handle_t*)&rctx->int_handler, NULL);

		if (&rctx->term_handler != NULL)
			uv_close((uv_handle_t*)&rctx->term_handler, NULL);

		uv_close((uv_handle_t*)&rctx->listener, NULL);

		h2o_context_request_shutdown(&rctx->ctx);

		// give open connections some time to close before using uv_stop
		uv_timer_t* stop_timer = malloc(sizeof(*stop_timer));
		if (stop_timer == NULL || uv_timer_init(&rctx->loop, stop_timer) < 0) {
			// try to exit asap
			uv_stop(&rctx->loop);
			return;
		}

		if (uv_timer_start(stop_timer, stop_loop, 5000, 0) < 0) {
			// also try to exit asap
			uv_close((uv_handle_t*)stop_timer, (uv_close_cb)free);
			uv_stop(&rctx->loop);
			return;
		}

		stop_timer->data = rctx;

		break;

	default:
		printf("Received some other signal\n");
		break;
	}
}

void h2ow__on_close(uv_handle_t* conn) {
	// this is called when a socket is closed; decrement the connections counter
	h2ow_run_context* rctx = ((uv_tcp_and_data*)conn)->more_data;
	rctx->num_connections--;

	free(conn);
}

void h2ow__on_accept(uv_stream_t* listener, int status) {
	uv_tcp_and_data* conn;
	h2o_socket_t* sock;
	h2ow_run_context* rctx = listener->data;

	if (unlikely(status != 0 || !rctx->wctx->is_running)) {
		return;
	}

	conn = malloc(sizeof(*conn));
	if (unlikely(conn == NULL)) {
		fprintf(stderr, "Out of memory\n");
		return;
	}

	// create a new connection object
	uv_tcp_init(&rctx->loop, (uv_tcp_t*)conn);
	conn->more_data = rctx;

	// try to accept a connection; thats why we got called
	if (unlikely(uv_accept(listener, (uv_stream_t*)conn) != 0)) {
		uv_close((uv_handle_t*)conn, (uv_close_cb)free);
		return;
	}

	// create an h2o_socket which is a wrapper for h2o's internals for sockets
	sock = h2o_uv_socket_create((uv_stream_t*)conn, h2ow__on_close);
	// and add the socket to the h2o context via the accept context
	h2o_accept(&rctx->accept_ctx, sock);

	// if we get here, we established a new connection; increment the connection counter
	rctx->num_connections++;
}

void* h2ow__per_thread_loop(void* arg) {
	thread_data* data = arg;
	h2ow_context* wctx = data->wctx;
	h2ow_run_context* rctx = &wctx->run_contexts[data->idx];

	uv_run(&rctx->loop, UV_RUN_DEFAULT);

	return NULL;
}

static int method_to_num(const char* method, int len) {
	// this will probably compile to a jump table, and then there are at max
	// 2 candidates for a given length, so this should be quite fast
	switch (len) {
	case 3:
		if (!memcmp(method, "GET", len)) return H2OW_METHOD_GET;
		if (!memcmp(method, "PUT", len)) return H2OW_METHOD_PUT;
		return -1;
		break;
	case 4:
		if (!memcmp(method, "POST", len)) return H2OW_METHOD_POST;
		if (!memcmp(method, "HEAD", len)) return H2OW_METHOD_HEAD;
		return -1;
		break;
	case 5:
		if (!memcmp(method, "PATCH", len)) return H2OW_METHOD_PATCH;
		if (!memcmp(method, "TRACE", len)) return H2OW_METHOD_TRACE;
		return -1;
		break;
	case 6:
		if (!memcmp(method, "DELETE", len)) return H2OW_METHOD_DELETE;
		return -1;
		break;
	case 7:
		if (!memcmp(method, "OPTIONS", len)) return H2OW_METHOD_OPTIONS;
		if (!memcmp(method, "CONNECT", len)) return H2OW_METHOD_CONNECT;
		return -1;
		break;
	default:
		return -1;
		break;
	}
}

// determine whether logging the string is ok or could cause problems
// (this should eventually be moved to a logging.c file when we get more
// sophisticated logging, see TODOs in settings.h)
static int is_string_safe(const char* str, int len) {
	for (int i = 0; i < len; i++) {
		if (str[i] < ' ' || str[i] > '~') return 0;
	}

	return 1;
}

int h2ow__request_handler(h2o_handler_t* self, h2o_req_t* req) {
	h2ow_handler_and_data* tmp = (h2ow_handler_and_data*)self;
	h2ow_run_context* rctx = tmp->more_data;
	h2ow_settings* settings = &rctx->wctx->settings;

	int method = method_to_num(req->method.base, req->method.len);

	if (unlikely(method == -1)) {
		H2OW_NOTE("Responding with 405 to an invalid/unsupported http method\n");

		req->res.status = 405;
		req->res.reason = "Method Not Allowed";
		h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE,
				NULL, H2O_STRLIT("text/plain"));
		h2o_send_inline(req, H2O_STRLIT("method not allowed :("));

		return 0;
	}

	char null_terminated_path[req->path.len + 1];
	memcpy(null_terminated_path, req->path.base, req->path.len);
	null_terminated_path[req->path.len] = '\0';

	h2ow_request_handler* handler = h2ow__find_matching_handler(&rctx->wctx->handlers,
			null_terminated_path, method);

	if (unlikely(handler == NULL)) {
		H2OW_NOTE("Sending 404 for a request to %s\n",
				is_string_safe(null_terminated_path, req->path.len)?
				null_terminated_path : "<contains unsafe characters>");

		req->res.status = 404;
		req->res.reason = "Not Found";
		h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE,
				NULL, H2O_STRLIT("text/plain"));
		h2o_send_inline(req, H2O_STRLIT("not found xd"));

		return 0;
	}

	handler->handler(req, rctx);

	return 0;
}

