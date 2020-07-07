#include <stdio.h>
#include <stdlib.h>

#include "h2ow.h"

void test_handler(h2o_req_t* req, __attribute__((unused)) h2ow_run_context* rctx) {
	req->res.status = 200;
	req->res.reason = "OK";
	h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE,
			NULL, H2O_STRLIT("text/plain"));
	h2o_send_inline(req, H2O_STRLIT("bruhh\n"));
}

void regex_handler(h2o_req_t* req, __attribute__((unused)) h2ow_run_context* rctx) {
	req->res.status = 200;
	req->res.reason = "OK";
	h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE,
			NULL, H2O_STRLIT("text/plain"));
	h2o_send_inline(req, H2O_STRLIT("aaaaaaaaaaa\n"));
}

int main() {
	h2ow_context context;
	h2ow_set_defaults(&context);
	h2ow_setopt(&context, H2OW_DEFAULT_HOST, "0.0.0.0", 8080);
	h2ow_setopt(&context, H2OW_DEBUG_LEVEL, H2OW_DEBUG_WARN);

	h2ow_register_handler(&context, H2OW_METHOD_ANY, "/bruh",
			H2OW_FIXED_PATH, test_handler);
	h2ow_register_handler(&context, H2OW_METHOD_ANY, "/a*",
			H2OW_WILDCARD_PATH, regex_handler);

	if (h2ow_run(&context) < 0) {
		printf("Error running server\n");
	}

	return 0;
}

