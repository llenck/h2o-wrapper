#ifndef _H2OW_SETTINGS_INCLUDED
#define _H2OW_SETTINGS_INCLUDED

#include "defs.h"

enum h2ow_settings {
	H2OW_DEFAULT_HOST,
	H2OW_THREAD_COUNT,
	// not implemented
	H2OW_SHUTDOWN_TIMEOUT,
	// partly implemented
	H2OW_DEBUG_LEVEL,
	// not implemented
	H2OW_LOG_FORMAT,
	H2OW_ADD_HOST,
	// almost implemented
	H2OW_SSL_CERT_AND_KEY,
	H2OW_SSL_PORT,
	H2OW_SSL_CIPHERS,
	H2OW_SSL_CTX
};

enum h2ow_debug_levels {
	H2OW_DEBUG_NONE,
	H2OW_DEBUG_ERR,
	H2OW_DEBUG_WARN,
	H2OW_DEBUG_NOTE
};

#define H2OW_ERR(...)                            \
	if (settings->debug_level >= H2OW_DEBUG_ERR) \
		fprintf(stderr, "[ERR]: " __VA_ARGS__);

#define H2OW_WARN(...)                            \
	if (settings->debug_level >= H2OW_DEBUG_WARN) \
		fprintf(stderr, "[WARN]: " __VA_ARGS__);

#define H2OW_NOTE(...)                            \
	if (settings->debug_level >= H2OW_DEBUG_NOTE) \
		fprintf(stderr, "[NOTE]: " __VA_ARGS__);

void h2ow_set_defaults(h2ow_context* wctx);
void h2ow_setopt(h2ow_context* wctx, int setting, ...);

#endif
