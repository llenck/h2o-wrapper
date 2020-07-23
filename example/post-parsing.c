#include <stdio.h>
#include <stdlib.h>

#include "h2ow.h"

const char form[]
        = "<!DOCTYPE html>\n"
          "<html>\n"
          "<head>\n"
          "\t<title>Test form :3</title>\n"
          "</head>\n"
          "<body>\n"
          "\t<form method=\"POST\">\n"
          "\t\t<input type=\"text\" name=\"test-field\" required><br>\n"
          "\t\t<button type=\"submit\" formaction=\"/cstr-post\">Submit to c-string-based handler</button>\n"
          "\t\t<button type=\"submit\" formaction=\"/vector-post\">Submit to vector-based handler</button>\n"
          "\t</form>\n"
          "</body>\n";
int form_len = sizeof(form) / sizeof(*form);

void send_error(h2o_req_t* req) {
	req->res.status = 400;
	req->res.reason = "Bad Request";
	h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE, NULL,
	               H2O_STRLIT("text/plain"));
	h2o_send_inline(req, H2O_STRLIT("we had an error parsing your request :(\n"));
}

void cstr_post_handler(h2o_req_t* req, __attribute__((unused)) h2ow_run_context* rctx) {
	// the post parsers of h2ow (both c string and vector variants) both overwrite
	// the original post body (the vector variant only does it to decode
	// x-www-form-urlencoded, while the c string variant also has to null-terminate
	// strings, and possibly has to copy the last key or value)
	h2ow_post_data data;
	if (h2ow_post_parse(req, &data) != 0) {
		send_error(req);
		return;
	}

	// a return value of NULL means the field was not set at all. if it didn't have a
	// value, it is treated as if it had an empty value (that is, "a&b=x" is parsed the
	// same as "a=&b=x")
	h2ow_post_field* field = h2ow_post_get(&data, "test-field");
	if (field == NULL) {
		send_error(req);
		return;
	}

	// allocating to the memory pool of a request is faster than malloc() and more
	// convenient as the memory pool is free'd when the request goes away.
	// it also might give better cache performance as things that are used together
	// (with this request) should be contiguous in memory
	char* response = h2ow_req_pool_alloc(req, 512);

	int len = snprintf(response, 512, "Found field with key %s and value %s\n",
	                   field->key, field->val);

	if (len < 0) {
		send_error(req);
		return;
	}

	req->res.status = 200;
	req->res.reason = "OK";
	h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE, NULL,
	               H2O_STRLIT("text/plain"));
	h2o_send_inline(req, response, len);
}

void vector_post_handler(h2o_req_t* req, __attribute__((unused)) h2ow_run_context* rctx) {
	// the vector variants of h2ow_post_* work with null bytes, are faster and
	// will probably at some point support file uploads via multipart/form-data.
	// For simple applications they are however a little more annoying to work with,
	// so you can safely go for the other variants if your code is already fast enough,
	// since the c string functions are easier to use correctly
	h2ow_post_vecs data;
	if (h2ow_post_parse_vecs(req, &data) != 0) {
		send_error(req);
		return;
	}

	// H2O_STRLIT("a") is expands to "a", strlen("a")
	h2ow_post_vec* field = h2ow_post_get_vec(&data, H2O_STRLIT("test-field"));
	if (field == NULL) {
		send_error(req);
		return;
	}

	char* response = h2ow_req_pool_alloc(req, 512);

	int len = snprintf(response, 512, "Found field with key %.*s and value %.*s\n",
	                   (int)field->key.len, field->key.base, (int)field->val.len,
	                   field->val.base);

	if (len < 0) {
		send_error(req);
		return;
	}

	req->res.status = 200;
	req->res.reason = "OK";
	h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE, NULL,
	               H2O_STRLIT("text/plain"));
	h2o_send_inline(req, response, len);
}

void form_handler(h2o_req_t* req, __attribute__((unused)) h2ow_run_context* rctx) {
	req->res.status = 200;
	req->res.reason = "OK";
	h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE, NULL,
	               H2O_STRLIT("text/html"));
	h2o_send_inline(req, form, form_len);
}

int main() {
	h2ow_context context;
	h2ow_set_defaults(&context);
	h2ow_setopt(&context, H2OW_DEFAULT_HOST, "0.0.0.0", 8080);

	h2ow_register_handler(&context, H2OW_METHOD_ANY, "/", H2OW_FIXED_PATH, form_handler);
	h2ow_register_handler(&context, H2OW_METHOD_POST, "/cstr-post", H2OW_FIXED_PATH,
	                      cstr_post_handler);
	h2ow_register_handler(&context, H2OW_METHOD_POST, "/vector-post", H2OW_FIXED_PATH,
	                      vector_post_handler);

	if (h2ow_run(&context) < 0) {
		printf("Error running server\n");
	}

	return 0;
}
