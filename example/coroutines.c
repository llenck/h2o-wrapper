#include <stdio.h>
#include <stdlib.h>

#include "h2ow.h"

void co_handler(h2o_req_t* req, __attribute__((unused)) h2ow_run_context* rctx,
                __attribute__((unused)) h2ow_resume_args* self) {
	req->res.status = 200;
	req->res.reason = "OK";
	h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE, NULL,
	               H2O_STRLIT("text/plain"));
	h2o_send_inline(req, H2O_STRLIT("hello, world from a coroutine!\n"));
}

int main() {
	h2ow_context context;
	h2ow_set_defaults(&context);
	h2ow_setopt(&context, H2OW_DEFAULT_HOST, "0.0.0.0", 8080);

	h2ow_register_handler_co(&context, H2OW_METHOD_ANY, "/hello-co", H2OW_FIXED_PATH,
	                         co_handler);

	if (h2ow_run(&context) < 0) {
		printf("Error running server\n");
	}

	return 0;
}
