#include "h2ow/settings.h"
#include "h2ow/handlers.h"

#include <stdio.h>
#include <stdarg.h>

#include <unistd.h>

void h2ow_set_defaults(h2ow_context* wctx) {
	h2ow__init_handler_lists(&wctx->handlers);

	h2ow_settings* settings = &wctx->settings;

	settings->ip = "127.0.0.1";
	settings->port = 8080;
	settings->thread_count = sysconf(_SC_NPROCESSORS_ONLN);
	settings->shutdown_timeout = 5000;
	settings->log_format = "";
	settings->debug_level = H2OW_DEBUG_WARN;

	wctx->is_running = 0;
}

void h2ow_setopt(h2ow_context* wctx, int setting, ...) {
	h2ow_settings* settings = &wctx->settings;
	va_list args;
	va_start(args, setting);

	switch (setting) {
	case H2OW_DEFAULT_HOST: {
		const char* ip = va_arg(args, const char*);
		int port = va_arg(args, int);
		settings->ip = ip;
		settings->port = port;
		break;
	}

	case H2OW_THREAD_COUNT: {
		int thread_count = va_arg(args, int);
		settings->thread_count = thread_count;
		break;
	}

	case H2OW_SHUTDOWN_TIMEOUT: {
		int timeout = va_arg(args, int);
		settings->shutdown_timeout = timeout;
		break;
	}

	case H2OW_LOG_FORMAT: {
		const char* fmt = va_arg(args, const char*);
		settings->log_format = fmt;
		break;
	}

	case H2OW_DEBUG_LEVEL: {
		int lvl = va_arg(args, int);
		settings->debug_level = lvl;
		break;
	}

	default: {
		if (settings->debug_level >= H2OW_DEBUG_WARN) {
			fprintf(stderr, "[warn]: ignoring unknown setting with number %d\n", setting);
		}
	} break;
	}
}
