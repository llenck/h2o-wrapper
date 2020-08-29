#include "h2ow/runtime.h"
#include "h2ow/settings.h"
#include "h2ow/handlers.h"
#include "h2ow/utils.h"
#include "h2ow/co-callbacks.h"

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
		H2OW_NOTE(
		        "Received termination signal, deleting listeners and signal handlers\n");
		H2OW_NOTE("(Delivery of another signal will now instantly kill the process)\n");

		// only close signal handlers if we registered them; we might add a feature
		// later which gives the user a choice in which signals are handled and how
		if (rctx->int_handler.data != NULL)
			uv_close((uv_handle_t*)&rctx->int_handler, NULL);

		if (rctx->term_handler.data != NULL)
			uv_close((uv_handle_t*)&rctx->term_handler, NULL);

		uv_close((uv_handle_t*)&rctx->listeners[0], NULL);
		if (rctx->wctx->ssl_ctx != NULL)
			uv_close((uv_handle_t*)&rctx->listeners[1], NULL);

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
	// and add the socket to the h2o context via the accept context (figure out which
	// context by getting the idx of the current listener in rctx->listeners)
	int ctx_idx = (uv_tcp_t*)listener - rctx->listeners;
	h2o_accept(&rctx->accept_ctxs[ctx_idx], sock);

	// if we get here, we established a new connection; increment the connection counter
	rctx->num_connections++;
}

static int method_to_num(const char* method, int len) {
	// this will probably compile to a jump table, and then there are at max
	// 2 candidates for a given length, so this should be quite fast
	switch (len) {
	case 3:
		if (!memcmp(method, "GET", len))
			return H2OW_METHOD_GET;
		if (!memcmp(method, "PUT", len))
			return H2OW_METHOD_PUT;
		return -1;
		break;
	case 4:
		if (!memcmp(method, "POST", len))
			return H2OW_METHOD_POST;
		if (!memcmp(method, "HEAD", len))
			return H2OW_METHOD_HEAD;
		return -1;
		break;
	case 5:
		if (!memcmp(method, "PATCH", len))
			return H2OW_METHOD_PATCH;
		if (!memcmp(method, "TRACE", len))
			return H2OW_METHOD_TRACE;
		return -1;
		break;
	case 6:
		if (!memcmp(method, "DELETE", len))
			return H2OW_METHOD_DELETE;
		return -1;
		break;
	case 7:
		if (!memcmp(method, "OPTIONS", len))
			return H2OW_METHOD_OPTIONS;
		if (!memcmp(method, "CONNECT", len))
			return H2OW_METHOD_CONNECT;
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
		if (str[i] < ' ' || str[i] > '~')
			return 0;
	}

	return 1;
}

static h2ow_co_and_stack* get_co_and_stack(h2ow_run_context* rctx) {
	if (rctx->co_wh != -1) {
		// if there is one free, advance wh by one and return the free one
		goto get_co;
	}

	// otherwise, we'll need to resize the pool
	int old_pool_len = rctx->co_pool_len;
	int new_pool_len = old_pool_len * 2;
	h2ow_co_and_stack* new_pool
	        = realloc(rctx->co_pool, new_pool_len * sizeof(*new_pool));
	if (new_pool == NULL) {
		// if there is no memory left, return NULL, indicating that we should probably
		// send a 500 status code
		return NULL;
	}

	for (int i = old_pool_len; i < new_pool_len; i++) {
		new_pool[i].prev = i - 1;
		new_pool[i].next = i + 1;
		new_pool[i].pool = &rctx->co_pool;

		if (unico_create_stack(&new_pool[i].stack, 4096 * 2) < 0) {
			// TODO graceful cleanup by reallocating new_pool to previous size, fixing
			// linked list and returning NULL
			exit(1);
		}
	}

	// corner cases not handled by loop; link last old element with the new elements and
	// mark the next field of the new last element as invalid
	new_pool[rctx->co_ah].next = old_pool_len;
	new_pool[new_pool_len - 1].next = -1;

	rctx->co_ah = new_pool_len - 1;
	rctx->co_pool = new_pool;
	rctx->co_pool_len = new_pool_len;

get_co:
	// now that the coroutine pool is big enough, do the same thing as if it had one free
	// element from the beginning
	rctx->co_pool_usage++;
	h2ow_co_and_stack* ret = &rctx->co_pool[rctx->co_wh];
	rctx->co_wh = ret->next;
	return ret;

cleanup:
	// see other TODO
	return NULL;
}

static void on_co_yield(h2ow_run_context* rctx, h2ow_co_and_stack* co) {
	if (!unico_is_finished(&co->co)) {
		return;
	}

	// free the coroutine object and recycle the stack to the pool
	unico_free_co(&co->co);

	h2ow_co_and_stack* pool = rctx->co_pool;
	int co_idx = co - pool;

	// link co->prev and co->next with each other and/or update rctx->co_{rh,wh,ah}
	if (co_idx == rctx->co_rh) {
		printf("setting co_rh to %d and pool[%d].prev to %d\n", rctx->co_rh, co->next,
		       co->prev);

		// if co was co_rh, we need to update co_rh as co is going to be moved to the
		// end of the list
		rctx->co_rh = co->next;

		// also, since the list can never have a size of 1, this means that co isn't
		// at the end of the linked list and co->next has to be -1; thus we can update
		// co->next without checking if it is -1
		pool[co->next].prev = co->prev;

		// append co to the end of the list
		pool[rctx->co_ah].next = co_idx;
		co->prev = rctx->co_ah;
		co->next = -1;
		rctx->co_ah = co_idx;
	}
	else {
		printf("setting pool[%d].next to %d ", co->prev, co->next);
		// if co wasn't rctx->co_rh, co->prev has to not be -1 since if it was, co
		// would be the first element of the linked list and thus rctx->co_rh
		pool[co->prev].next = co->next;

		// in this case, we also have to check whether the coroutine is at the end of
		// the list
		if (co_idx == rctx->co_ah) {
			printf("and co_wh to %d\n", co_idx);
			// if co is the last element in the list, we need to update rctx->co_wh as
			// it is currently -1, as there are no free elements left in the linked list.
			rctx->co_wh = co_idx;
		}
		else {
			printf("and pool[%d].prev to %d\n", co->next, co->prev);
			// co is somewhere in the middle, and we can safely update co->next
			pool[co->next].prev = co->prev;

			// append co to the end of the list
			// (lets hope the compiler deduplicates this code lol)
			pool[rctx->co_ah].next = co_idx;
			co->prev = rctx->co_ah;
			co->next = -1;
			rctx->co_ah = co_idx;
		}
	}

	rctx->co_pool_usage--;

	if (rctx->co_pool_usage < rctx->co_pool_len / 4 && rctx->co_pool_usage > 16) {
		// TODO shrink the pool
	}
}

static void unico_helper(unico_co_state* self) {
	h2ow_co_params* params = self->data;
	// copy the contents of params, as the memory will be invalid after our first yield
	void (*cb)(h2o_req_t*, h2ow_run_context*, h2ow_resume_args*) = params->handler;
	h2o_req_t* req = params->req;
	int self_idx = params->idx;
	h2ow_run_context* rctx = params->rctx;

	h2ow_co_and_stack* self_with_stack = &rctx->co_pool[self_idx];

	self_with_stack->is_dead = 0;

	// allocate memory for return values from async calls to the request, which should
	// give better cache performance than allocating them statically
	h2ow_resume_args* res_args = h2ow_req_pool_alloc(req, sizeof(*res_args));
	self_with_stack->resume_args = res_args;

	res_args->pool = &rctx->co_pool;
	res_args->idx = self_idx;
	res_args->waiting_for_proceed = 0;

	res_args->super.proceed = h2ow__co_resume_proceed;
	res_args->super.stop = h2ow__co_resume_stop;
	h2o_start_response(req, &res_args->super);

	cb(req, rctx, res_args);

	// at this point, if cb yielded and other coroutines were called, the current
	// h2ow_co_and_stack might have been relocated, and we can't safely use self
	// anymore, so we recompute it
	self = &rctx->co_pool[self_idx].co;

	// on x86 the expansion of unico_exit causes scan-build to report a dead store
	// on self, so tell it to not do so
	(void)self;

	unico_exit(self);
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
		h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE, NULL,
		               H2O_STRLIT("text/plain"));
		h2o_send_inline(req, H2O_STRLIT("method not allowed :("));

		return 0;
	}

	char null_terminated_path[req->path.len + 1];
	memcpy(null_terminated_path, req->path.base, req->path.len);
	null_terminated_path[req->path.len] = '\0';

	h2ow_request_handler* handler = h2ow__find_matching_handler(
	        &rctx->wctx->handlers, null_terminated_path, method);

	if (unlikely(handler == NULL)) {
		H2OW_NOTE("Sending 404 for a request to %s\n",
		          is_string_safe(null_terminated_path, req->path.len) ?
		                  null_terminated_path :
		                  "<contains unsafe characters>");

		req->res.status = 404;
		req->res.reason = "Not Found";
		h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE, NULL,
		               H2O_STRLIT("text/plain"));
		h2o_send_inline(req, H2O_STRLIT("not found xd"));

		return 0;
	}

	if (handler->call_type == H2OW_HANDLER_NORMAL) {
		handler->sync_handler(req, rctx);
	}
	else if (handler->call_type == H2OW_HANDLER_CO) {
		h2ow_co_and_stack* co = get_co_and_stack(rctx);
		if (unlikely(co == NULL)) {
			H2OW_WARN("Sending 500 for a request to %s\n",
			          is_string_safe(null_terminated_path, req->path.len) ?
			                  null_terminated_path :
			                  "<contains unsafe characters>");

			req->res.status = 500;
			req->res.reason = "Internal Server Error";
			h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE, NULL,
			               H2O_STRLIT("text/plain"));
			h2o_send_inline(req, H2O_STRLIT("internal server error :("));

			return 0;
		}

		// unico_helper makes a copy of these params, so it is ok to not keep them in
		// memory after it yields the first time
		h2ow_co_params params = { req, rctx, handler->co_handler, rctx->co_pool - co };
		co->co.data = &params;
		unico_create_co(&co->co, &co->stack, unico_helper);

		// while it is unlikely, check whether the coroutine is finished
		on_co_yield(rctx, co);
	}
	else {
		H2OW_NOTE("Sending 500 for a request to %s\n",
		          is_string_safe(null_terminated_path, req->path.len) ?
		                  null_terminated_path :
		                  "<contains unsafe characters>");

		req->res.status = 500;
		req->res.reason = "Internal Server Error";
		h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE, NULL,
		               H2O_STRLIT("text/plain"));
		h2o_send_inline(req, H2O_STRLIT("internal server error ):"));
		return 0;
	}

	return 0;
}
