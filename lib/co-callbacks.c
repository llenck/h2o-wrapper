#include "h2ow/co-callbacks.h"

#include <unico.h>

void h2ow_co_resume(uv_handle_t* handle) {
	h2ow_resume_args* args = handle->data;
	h2ow_co_and_stack* pool = *args->pool;

	unico_resume(&pool[args->idx].co);
}
