#include "h2ow/co-callbacks.h"

#include <unico.h>

void h2ow_co_resume(uv_handle_t* handle) {
	h2ow_resume_args* args = handle->data;
	h2ow_co_and_stack* pool = *args->pool;

	unico_resume(&pool[args->idx].co);
}

void h2ow_co_resume_i(uv_handle_t* handle, int status) {
	h2ow_resume_args* args = handle->data;
	h2ow_co_and_stack* pool = *args->pool;

	args->res.i.status = status;

	unico_resume(&pool[args->idx].co);
}

void h2ow_co_resume_poll(uv_poll_t* handle, int status, int events) {
	h2ow_resume_args* args = handle->data;
	h2ow_co_and_stack* pool = *args->pool;

	args->res.poll.status = status;
	args->res.poll.events = events;

	unico_resume(&pool[args->idx].co);
}

void h2ow_co_resume_read(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf) {
	h2ow_resume_args* args = handle->data;
	h2ow_co_and_stack* pool = *args->pool;

	args->res.read.nread = nread;
	args->res.read.buf = buf;

	unico_resume(&pool[args->idx].co);
}

void h2ow_co_resume_exit(uv_process_t* handle, int64_t status, int term_sig) {
	h2ow_resume_args* args = handle->data;
	h2ow_co_and_stack* pool = *args->pool;

	args->res.exit.status = status;
	args->res.exit.term_sig = term_sig;

	unico_resume(&pool[args->idx].co);
}

void h2ow_co_resume_getaddrinfo(uv_getaddrinfo_t* handle, int status,
                                struct addrinfo* res) {
	h2ow_resume_args* args = handle->data;
	h2ow_co_and_stack* pool = *args->pool;

	args->res.getaddrinfo.status = status;
	args->res.getaddrinfo.res = res;

	unico_resume(&pool[args->idx].co);
}

void h2ow_co_resume_getnameinfo(uv_getnameinfo_t* handle, int status,
                                const char* hostname, const char* service) {
	h2ow_resume_args* args = handle->data;
	h2ow_co_and_stack* pool = *args->pool;

	args->res.getnameinfo.status = status;
	args->res.getnameinfo.hostname = hostname;
	args->res.getnameinfo.service = service;

	unico_resume(&pool[args->idx].co);
}

void h2ow_co_resume_fs_event(uv_fs_event_t* handle, const char* filename, int events,
                             int status) {
	h2ow_resume_args* args = handle->data;
	h2ow_co_and_stack* pool = *args->pool;

	args->res.fs_event.filename = filename;
	args->res.fs_event.events = events;
	args->res.fs_event.status = status;

	unico_resume(&pool[args->idx].co);
}

void h2ow_co_resume_fs_poll(uv_fs_poll_t* handle, int status, const uv_stat_t* prev,
                            const uv_stat_t* curr) {
	h2ow_resume_args* args = handle->data;
	h2ow_co_and_stack* pool = *args->pool;

	args->res.fs_poll.prev = prev;
	args->res.fs_poll.curr = curr;

	unico_resume(&pool[args->idx].co);
}

void h2ow_co_resume_signal(uv_signal_t* handle, int signum) {
	h2ow_resume_args* args = handle->data;
	h2ow_co_and_stack* pool = *args->pool;

	args->res.signal.signum = signum;

	unico_resume(&pool[args->idx].co);
}

void h2ow_co_resume_udp_recv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                             const struct sockaddr* addr, unsigned flags) {
	h2ow_resume_args* args = handle->data;
	h2ow_co_and_stack* pool = *args->pool;

	args->res.udp_recv.nread = nread;
	args->res.udp_recv.buf = buf;
	args->res.udp_recv.addr = addr;
	args->res.udp_recv.flags = flags;

	unico_resume(&pool[args->idx].co);
}

void h2ow__co_resume_proceed(h2o_generator_t* self,
                             __attribute__((unused)) h2o_req_t* req) {
	h2ow_resume_args* args = (h2ow_resume_args*)self;
	h2ow_co_and_stack* pool = *args->pool;

	if (args->waiting_for_proceed) {
		args->waiting_for_proceed = 0;
		unico_resume(&pool[args->idx].co);
	}
}

void h2ow__co_resume_stop(h2o_generator_t* self, __attribute__((unused)) h2o_req_t* req) {
	h2ow_resume_args* args = (h2ow_resume_args*)self;
	h2ow_co_and_stack* pool = *args->pool;
	pool[args->idx].is_dead = 1;

	// if the application is waiting for a libuv callback, don't resume it, but
	// do so if it waited for the connection being ready for more data, since
	// the proceed callback won't be called in that case
	if (args->waiting_for_proceed) {
		args->waiting_for_proceed = 0;
		unico_resume(&pool[args->idx].co);
	}
}
