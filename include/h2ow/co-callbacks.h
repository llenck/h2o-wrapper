#ifndef _CO_CALLBACKS_INCLUDED
#define _CO_CALLBACKS_INCLUDED

#include "defs.h"

#include <sys/types.h>
#include <uv.h>

// for uv_{close, timer, async, prepare, check, idle, fs, work}_cb
void h2ow_co_resume(uv_handle_t* handle);
// for uv_{write, connect, shutdown, connection}_cb
void h2ow_co_resume_i(uv_handle_t* handle, int status);

void h2ow_co_resume_poll(uv_poll_t* handle, int status, int events);
void h2ow_co_resume_read(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf);
void h2ow_co_resume_exit(uv_process_t* proc, int64_t status, int term_sig);
void h2ow_co_resume_getaddrinfo(uv_getaddrinfo_t* handle, int status,
                                struct addrinfo* res);
void h2ow_co_resume_getnameinfo(uv_getnameinfo_t* handle, int status,
                                const char* hostname, const char* service);

#endif
