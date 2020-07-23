#include <h2ow/uthash.h>

#include <h2ow/utils.h>

char hex_to_num[256]
        = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,
	        9,  -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1,
	        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	        -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	        -1, -1, -1, -1, -1, -1, -1, -1, -1 };

// kinda hacky, but should work
#undef uthash_malloc
#define uthash_malloc(n) h2o_mem_alloc_shared(&req->pool, n, NULL)
#undef uthash_free
#define uthash_free(p, n) ((void)0)

static int parse_urlencoded_form_data(h2o_req_t* req, h2ow_post_data* data) {
	// read head, write head
	char *rh = req->entity.base, *wh = rh;
	size_t len = req->entity.len;
	char* end = rh + len;

	if (rh == end)
		return -1;

	// loop until rh reaches end, copying from rh to wh, undoing the url encoding
	while (rh < end) {
		char* name = wh;
		int name_len;
		char* val = NULL;

		// loop until the end of the field name
		while (*rh != '=') {
			// check if we're at the end of the string
			if (rh == end - 1) {
				name_len = wh - name + 1;
				char* tmp = h2o_mem_alloc_shared(&req->pool, name_len + 1, NULL);
				memcpy(tmp, name, name_len);
				tmp[name_len] = '\0';
				name = tmp;

				rh = end;
				goto insert_val;
			}

			switch (*rh) {
			// check if this is a key without a value
			case '&':
				name_len = wh - name;
				goto after_val;
				break;

			// the rest of this switch is urlencoded parsing
			case '+':
				*wh = ' ';
				wh++, rh++;
				break;

			case '%':
				if (rh >= end - 3) {
					return -1;
				}

				unsigned char* urh = (unsigned char*)rh;
				int a = hex_to_num[urh[1]], b = hex_to_num[urh[2]];
				if (a == -1 || b == -1) {
					return -1;
				}
				*wh = (a << 4) | b;
				wh++, rh += 3;
				break;

			default:
				// just copy the char over
				*wh = *rh;
				wh++, rh++;
				break;
			}
		}

		name_len = wh - name;
		*wh = '\0';
		wh++, rh++;

		// check if someone put an '=' at the end of the post data; in that case, the
		// next loop would go into uninitialized memory, so skip that and let val point
		// to the null byte we just made
		if (rh == end) {
			val = wh - 1;
			goto insert_val;
		}

		val = wh;
		// loop until the end of the value
		while (*rh != '&') {
			// check if we're at the end of the string
			if (rh == end - 1) {
				int val_len = wh - val + 1;
				char* tmp = h2o_mem_alloc_shared(&req->pool, val_len + 1, NULL);
				memcpy(tmp, val, val_len);
				tmp[val_len] = '\0';
				val = tmp;

				rh = end;
				goto insert_val;
			}

			// parse urlencoded stuff
			switch (*rh) {
			case '+':
				*wh = ' ';
				wh++, rh++;
				break;

			case '%':
				if (rh >= end - 3) {
					return -1;
				}

				unsigned char* urh = (unsigned char*)rh;
				int a = hex_to_num[urh[1]], b = hex_to_num[urh[2]];
				if (a == -1 || b == -1) {
					return -1;
				}
				*wh = (a << 4) | b;
				wh++, rh += 3;
				break;

			default:
				*wh = *rh;
				wh++, rh++;
				break;
			}
		}

	after_val:
		*wh = '\0';

	insert_val:
		// rh at this point: at an & sign (or, if this is the last field, end)

		// handle someone putting "&=" or "&&" into their post data
		if (name_len <= 0)
			return -1;
		// allocate a new h2ow_post_field
		h2ow_post_field* new_field
		        = h2o_mem_alloc_shared(&req->pool, sizeof(*new_field), NULL);
		new_field->key = name;
		new_field->val = val;

		// save the new key/value pair
		HASH_ADD_KEYPTR(hh, data->fields, name, name_len, new_field);

		// point to the next key for the next iteration
		wh++, rh++;
	}

	return 0;
}

// convenience function for parsing post data (currently only www-form-urlencoded)
// that doesn't contain any null bytes
int h2ow_post_parse(h2o_req_t* req, h2ow_post_data* data) {
	data->fields = NULL;

	return parse_urlencoded_form_data(req, data);
}

h2ow_post_field* h2ow_post_get(h2ow_post_data* data, const char* key) {
	h2ow_post_field* ret = NULL;
	HASH_FIND_STR(data->fields, key, ret);
	return ret;
}

static int parse_urlencoded_form_vecs(h2o_req_t* req, h2ow_post_vecs* data) {
	// read head, write head
	char *rh = req->entity.base, *wh = rh;
	size_t len = req->entity.len;
	char* end = rh + len;

	if (rh == end)
		return -1;

	// loop until rh reaches end, copying from rh to wh, undoing the url encoding
	while (rh < end) {
		char* name = wh;
		int name_len;
		char* val = NULL;
		int val_len;

		// loop until the end of the field name
		while (*rh != '=' && rh < end) {
			switch (*rh) {
			// check if this is a key without a value
			case '&':
				name_len = wh - name;
				val_len = 0;
				goto insert_val;
				break;

			// the rest of this switch is urlencoded parsing
			case '+':
				*wh = ' ';
				wh++, rh++;
				break;

			case '%':
				if (rh >= end - 3) {
					return -1;
				}

				unsigned char* urh = (unsigned char*)rh;
				int a = hex_to_num[urh[1]], b = hex_to_num[urh[2]];
				if (a == -1 || b == -1) {
					return -1;
				}
				*wh = (a << 4) | b;
				wh++, rh += 3;
				break;

			default:
				// just copy the char over
				*wh = *rh;
				wh++, rh++;
				break;
			}
		}

		// handle name_len == 0 later since then, we'd want to skip over the value
		name_len = wh - name;
		wh++, rh++;

		val = wh;
		// loop until the end of the value
		while (*rh != '&' && rh < end) {
			// parse urlencoded stuff
			switch (*rh) {
			case '+':
				*wh = ' ';
				wh++, rh++;
				break;

			case '%':
				if (rh >= end - 3) {
					return -1;
				}

				unsigned char* urh = (unsigned char*)rh;
				int a = hex_to_num[urh[1]], b = hex_to_num[urh[2]];
				if (a == -1 || b == -1) {
					return -1;
				}
				*wh = (a << 4) | b;
				wh++, rh += 3;
				break;

			default:
				*wh = *rh;
				wh++, rh++;
				break;
			}
		}

		val_len = wh - val;

	insert_val:
		// rh at this point: at an & sign (or, if this is the last field, end)

		// handle someone putting "&=" or "&&" into their post data
		if (name_len <= 0) {
			wh++, rh++;
			continue;
		}

		// allocate a new h2ow_post_vec
		h2ow_post_vec* new_field
		        = h2o_mem_alloc_shared(&req->pool, sizeof(*new_field), NULL);
		new_field->key.base = name;
		new_field->key.len = name_len;
		new_field->val.base = val;
		new_field->val.len = val_len;

		// save the new key/value pair
		HASH_ADD_KEYPTR(hh, data->fields, name, name_len, new_field);

		// point to the next key for the next iteration
		wh++, rh++;
	}

	return 0;
}

int h2ow_post_parse_vecs(h2o_req_t* req, h2ow_post_vecs* data) {
	data->fields = NULL;

	// TODO support multipart/form-data
	return parse_urlencoded_form_vecs(req, data);
}

h2ow_post_vec* h2ow_post_get_vec(h2ow_post_vecs* data, const char* key,
                                 unsigned int key_len) {
	h2ow_post_vec* ret = NULL;
	HASH_FIND(hh, data->fields, key, key_len, ret);
	return ret;
}
