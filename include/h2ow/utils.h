#ifndef _H2OW_UTILS_INCLUDED
#define _H2OW_UTILS_INCLUDED

#include <h2o.h>
#include "defs.h"

#include "uthash.h"

// null-terminated post x-www-form-urlencoded parsing
typedef struct h2ow_post_field_s {
	char* key;
	char* val;
	UT_hash_handle hh;
} h2ow_post_field;

typedef struct h2ow_post_data_s {
	h2ow_post_field* fields;
} h2ow_post_data;

int h2ow_post_parse(h2o_req_t* req, h2ow_post_data* data);
h2ow_post_field* h2ow_post_get(h2ow_post_data* data, const char* key);

// vector (pointer + len) based post x-www-form-urlencoded parsing
// (multipart/form-data parsing might be added later)
typedef struct h2ow_post_vec_s {
	h2o_iovec_t key;
	h2o_iovec_t val;
	UT_hash_handle hh;
} h2ow_post_vec;

typedef struct h2ow_post_vecs_s {
	h2ow_post_vec* fields;
} h2ow_post_vecs;

int h2ow_post_parse_vecs(h2o_req_t* req, h2ow_post_vecs* data);
h2ow_post_vec* h2ow_post_get_vec(h2ow_post_vecs* data, const char* key,
                                 unsigned int key_len);

#define h2ow_req_is_ssl(x) ((x)->scheme == &H2O_URL_SCHEME_HTTPS)
#define h2ow_req_pool_alloc(req, n) h2o_mem_alloc_shared(&(req)->pool, n, NULL)

#endif
