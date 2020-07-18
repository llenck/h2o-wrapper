#include <stdio.h>
#include <stdlib.h>

#include "h2ow.h"

void test_handler(h2o_req_t* req, __attribute__((unused)) h2ow_run_context* rctx) {
	req->res.status = 200;
	req->res.reason = "OK";
	h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE, NULL,
	               H2O_STRLIT("text/plain"));

	char* buffer = h2o_mem_alloc_shared(&req->pool, 256, NULL);

	// h2o saves the version as 0xMMmm, where M is the mayor version and m is the minor version
	int len = snprintf(buffer, 256, "Connected over %s using HTTP/%d.%d\n",
	                   h2ow_req_is_ssl(req) ? "ssl/tls" : "plain tcp", req->version >> 8,
	                   req->version & 0xFF);

	if (len < 0)
		h2o_send_inline(req, H2O_STRLIT("An error occured"));
	else
		h2o_send_inline(req, buffer, len);
}

int main(int argc, char** argv) {
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <path-to-cert.pem> <path-to-privkey.pem>\n", argv[0]);

		return 1;
	}

	h2ow_context context;
	h2ow_set_defaults(&context);
	h2ow_setopt(&context, H2OW_DEFAULT_HOST, "0.0.0.0", 8080);
	h2ow_setopt(&context, H2OW_SSL_CERT_AND_KEY, argv[1], argv[2]);
	h2ow_setopt(&context, H2OW_SSL_PORT, 8443);

	h2ow_register_handler(&context, H2OW_METHOD_ANY, "/test-ssl", H2OW_FIXED_PATH,
	                      test_handler);

	if (h2ow_run(&context) < 0) {
		printf("Error running server\n");
	}

	return 0;
}
