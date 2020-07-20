#ifndef _H2OW_UTILS_INCLUDED
#define _H2OW_UTILS_INCLUDED

#include <h2o.h>
#include "defs.h"

#define h2ow_req_is_ssl(x) ((x)->scheme == &H2O_URL_SCHEME_HTTPS)
#define h2ow_req_pool_alloc(req, n) h2o_mem_alloc_shared(&(req)->pool, n, NULL)

#endif
