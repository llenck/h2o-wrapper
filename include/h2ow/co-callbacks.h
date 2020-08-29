#ifndef _CO_CALLBACKS_INCLUDED
#define _CO_CALLBACKS_INCLUDED

#include "defs.h"

#include <sys/types.h>
#include <uv.h>

// these functions below are to be used by the application

// i don't know whether libuv guarantees this, we we don't need seperate functions
// for cbs that are passed requests instead of handles since either way, the data field
// has the same offset (0)

// for uv_{close, timer, async, prepare, check, idle, fs}_cb
void h2ow_co_resume(uv_handle_t* handle);
// for uv_{after_work, write, connect, shutdown, connection, udp_send}_cb
void h2ow_co_resume_i(uv_handle_t* handle, int status);

void h2ow_co_resume_poll(uv_poll_t* handle, int status, int events);
void h2ow_co_resume_read(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf);
void h2ow_co_resume_exit(uv_process_t* proc, int64_t status, int term_sig);
void h2ow_co_resume_getaddrinfo(uv_getaddrinfo_t* handle, int status,
                                struct addrinfo* res);
void h2ow_co_resume_getnameinfo(uv_getnameinfo_t* handle, int status,
                                const char* hostname, const char* service);

void h2ow_co_resume_fs_event(uv_fs_event_t* handle, const char* filename, int events,
                             int status);
void h2ow_co_resume_fs_poll(uv_fs_poll_t* handle, int status, const uv_stat_t* prev,
                            const uv_stat_t* curr);
void h2ow_co_resume_signal(uv_signal_t* handle, int signum);

void h2ow_co_resume_udp_recv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                             const struct sockaddr* addr, unsigned flags);

// the next few functions are used by h2ow itself

void h2ow__co_resume_proceed(h2o_generator_t* self, h2o_req_t* req);
void h2ow__co_resume_stop(h2o_generator_t* self, h2o_req_t* req);

#endif
