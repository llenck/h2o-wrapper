#include "h2ow/runtime.h"
#include "h2ow/settings.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

static int create_signal_handler(h2ow_run_context* rctx, int signum) {
	uv_signal_t* signal_handler;
	switch (signum) {
	case SIGINT:
		signal_handler = &rctx->int_handler;
		break;

	case SIGTERM:
		signal_handler = &rctx->term_handler;
		break;

	default:
		return -1;
		break;
	}

	if (uv_signal_init(&rctx->loop, signal_handler) < 0) {
		return -1;
	}
	if (uv_signal_start(signal_handler, h2ow__on_signal, signum) < 0) {
		uv_close((uv_handle_t*)signal_handler, NULL);
		return -1;
	}

	signal_handler->data = rctx;

	return 0;
}

static int create_reuseaddr_socket(h2ow_run_context* rctx) {
	const h2ow_settings* settings = &rctx->wctx->settings;
	int fd;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1) {
		H2OW_ERR("Couldn't open socket\n");
		return -1;
	}

	int reuse_addr = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuse_addr, sizeof(reuse_addr)) == -1) {
		H2OW_ERR("Couldn't set SO_REUSEPORT flag on socket\n");
		close(fd);
		return -1;
	}

	return fd;
}

static int create_listener(h2ow_run_context* rctx, int ssl) {
	const h2ow_settings* settings = &rctx->wctx->settings;
	struct sockaddr_in addr;
	uv_tcp_t* listener = &rctx->listeners[ssl ? 1 : 0];

	// create a libuv handle to later assign a socket to
	if (uv_tcp_init(&rctx->loop, listener) < 0) {
		H2OW_ERR("Couldn't create a new libuv handle for the socket\n");
		return -1;
	}

	// create an address that we can later bind the socket to
	if (uv_ip4_addr(settings->ip, ssl ? settings->ssl_port : settings->port, &addr) < 0) {
		H2OW_ERR("Couldn't get address to bind to\n");
		return -1;
	}

	// create a socket and an address for it
	int sockfd = create_reuseaddr_socket(rctx);
	if (sockfd == -1) {
		H2OW_ERR("Couldn't open socket\n");
		close(sockfd);
		return -1;
	}

	// open the socket as a libuv handle
	if (uv_tcp_open(listener, sockfd) < 0) {
		H2OW_ERR("Couldn't assign the socket to the libuv handle\n");
		close(sockfd);
		return -1;
	}

	// from now on, if we have an error, we need to run the uv loop to clean
	// up the handle (since uv_close is of course asynchronous).

	// pass rctx to the listener
	listener->data = rctx;

	// bind/listen on the listener
	if (uv_tcp_bind(listener, (struct sockaddr*)&addr, 0) != 0) {
		H2OW_ERR("Couldn't bind socket\n");
		goto err;
	}

	if (uv_listen((uv_stream_t*)listener, 128, h2ow__on_accept) != 0) {
		H2OW_ERR("Couldn't listen on bound socket\n");
		goto err;
	}

	return 0;

err:
	rctx->running_cleanup_cbs = 1;
	uv_close((uv_handle_t*)listener, h2ow__cleanup_cb);
	uv_run(&rctx->loop, UV_RUN_DEFAULT);
	return -1;
}

static void register_handler(h2ow_run_context* rctx) {
	h2o_pathconf_t* pc = h2o_config_register_path(rctx->hostconf, "/", 0);
	rctx->root_handler
	        = (h2ow_handler_and_data*)h2o_create_handler(pc, sizeof(*rctx->root_handler));
	rctx->root_handler->super.on_req = h2ow__request_handler;
	rctx->root_handler->more_data = rctx;
}

static void delete_handler(h2ow_run_context* rctx) {
	free(rctx->root_handler);
}

static void init_openssl_once() {
	static int is_initialized = 0;
	if (__sync_val_compare_and_swap(&is_initialized, 0, 1)) {
		SSL_load_error_strings();
		SSL_library_init();
		OpenSSL_add_all_algorithms();
	}
}

static int try_create_ssl_ctx(h2ow_context* wctx) {
	h2ow_settings* settings = &wctx->settings;

	// either copy the pointer to a user-provided ssl context, or create a new one
	// if ssl_cert_path and ssl_key_path are given
	if (settings->ssl_ctx != NULL) {
		wctx->ssl_ctx = settings->ssl_ctx;
	}
	else if (settings->ssl_cert_path != NULL && settings->ssl_key_path != NULL) {
		init_openssl_once();

		wctx->ssl_ctx = SSL_CTX_new(TLS_server_method());
		// sorry for the looks of this but this is what clang-format wants it to look
		// like and its still better than doing a million seperate ifs
		if (wctx->ssl_ctx == NULL
		    || SSL_CTX_use_certificate_chain_file(wctx->ssl_ctx, settings->ssl_cert_path)
		               != 1
		    || SSL_CTX_use_PrivateKey_file(wctx->ssl_ctx, settings->ssl_key_path,
		                                   SSL_FILETYPE_PEM)
		               != 1)
		{
			unsigned int err = ERR_get_error();
			H2OW_ERR("couldn't create ssl context because %s failed: %s\n",
			         ERR_func_error_string(err), ERR_reason_error_string(err));

			return -1;
		}
	}
	else if (settings->ssl_cert_path != NULL || settings->ssl_key_path != NULL) {
		H2OW_WARN(
		        "only one of the cert path and the private key path were provided, continuing without ssl\n");
	}

	return 0;
}

static int allocate_wctx_buffers(h2ow_context* wctx) {
	h2ow_settings* settings = &wctx->settings;
	int num_threads = settings->thread_count;

	wctx->run_contexts = calloc(num_threads, sizeof(*wctx->run_contexts));
	if (wctx->run_contexts == NULL) {
		H2OW_ERR("not enough memory to allocate run contexts\n");

		return -1;
	}

	wctx->threads = malloc(num_threads * sizeof(*wctx->threads));
	if (wctx->threads == NULL) {
		H2OW_ERR("not enough memory to allocate pthread handles\n");

		free(wctx->run_contexts);
		return -1;
	}

	return 0;
}

// adds the number of additional uv_close calls to running_cleanup_cbs
static void close_uv_listeners(h2ow_run_context* rctx) {
	uv_close((uv_handle_t*)&rctx->listeners[0], h2ow__cleanup_cb);
	rctx->running_cleanup_cbs++;

	if (rctx->wctx->ssl_ctx != NULL) {
		uv_close((uv_handle_t*)&rctx->listeners[1], h2ow__cleanup_cb);
		rctx->running_cleanup_cbs++;
	}
}

static void run_all_threads(h2ow_context* wctx, thread_data* thread_infos) {
	h2ow_settings* settings = &wctx->settings;
	int num_threads = settings->thread_count;
	int threads_started[num_threads];
	memset(threads_started, 0, num_threads);

	// signal some handlers to actually do stuff (which they shouldn't if we just
	// ran the uv loop for cleaning up after a fatal error)
	wctx->is_running = 1;

	// start the other threads (skip the first, since this thread becomes #1)
	for (int i = 1; i < num_threads; i++) {
		int tmp = pthread_create(&wctx->threads[i], NULL, h2ow__per_thread_loop,
		                         &thread_infos[i]);

		// continue anyway if pthread_create fails, but remember
		// whether we should pthread_join
		threads_started[i] = !tmp;
		if (tmp != 0) {
			H2OW_WARN("failed to create thread %d, continuing anyway\n", i);
		}
	}

	// this thread becomes thread #1
	h2ow__per_thread_loop(&thread_infos[0]);

	for (int i = 1; i < num_threads; i++) {
		if (threads_started[i]) {
			pthread_join(wctx->threads[i], NULL);
		}
	}
}

int h2ow_run(h2ow_context* wctx) {
	int ret = 0;
	int cleanup_until = -1; // inclusive
	h2ow_settings* settings = &wctx->settings;
	int num_threads = settings->thread_count;
	thread_data thread_infos[num_threads];

	if (allocate_wctx_buffers(wctx) < 0) {
		// nothing is allocated / initialized yet so we don't need any cleanup
		return -1;
	}

	if (try_create_ssl_ctx(wctx) < 0) {
		ret = -5;
		goto cleanup;
	}

	// now init stuff per thread
	if (num_threads <= 0) {
		H2OW_ERR("num_threads is %d, exiting\n", num_threads);
		ret = -2;
		goto cleanup;
	}

	for (int i = 0; i < num_threads; i++) {
		h2ow_run_context* rctx = &wctx->run_contexts[i];

		thread_infos[i].wctx = wctx;
		thread_infos[i].idx = i;

		// first init some fields of rctx that we know already
		rctx->wctx = wctx;
		rctx->num_connections = 0;

		// h2o initialization depends on a uv loop, so init that second
		if (uv_loop_init(&rctx->loop) < 0) {
			H2OW_ERR("Failed to init uv loop for thread %d\n", i);

			ret = -3;
			goto cleanup;
		}

		// create h2o config
		h2o_config_init(&rctx->globconf);
		rctx->globconf.http1.upgrade_to_http2 = 1;

		// init other h2o stuff
		rctx->hostconf = h2o_config_register_host(
		        &rctx->globconf, h2o_iovec_init(H2O_STRLIT(wctx->settings.ip)),
		        wctx->settings.port);

		h2o_context_init(&rctx->ctx, &rctx->loop, &rctx->globconf);

		rctx->accept_ctxs[0].ctx = &rctx->ctx;
		rctx->accept_ctxs[0].hosts = rctx->globconf.hosts;
		if (wctx->ssl_ctx != NULL) {
			rctx->accept_ctxs[1].ctx = &rctx->ctx;
			rctx->accept_ctxs[1].hosts = rctx->globconf.hosts;
			rctx->accept_ctxs[1].ssl_ctx = wctx->ssl_ctx;
		}

		register_handler(rctx);

		if (create_listener(rctx, 0) < 0) {
			H2OW_ERR("Failed to create plain http listener for thread %d\n", i);

			ret = -4;
			goto cleanup;
		}
		if (wctx->ssl_ctx != NULL) {
			if (create_listener(rctx, 1) < 0) {
				H2OW_ERR("Failed to create ssl listener for thread %d\n", i);

				rctx->running_cleanup_cbs = 1;
				uv_close((uv_handle_t*)&rctx->listeners[0], h2ow__cleanup_cb);
				uv_run(&rctx->loop, UV_RUN_DEFAULT);
				// can't close the loop since h2o registers some handles to the uv loop

				ret = -4;
				goto cleanup;
			}
		}

		if (create_signal_handler(rctx, SIGINT) < 0) {
			H2OW_ERR("Failed to register SIGINT handler for thread %d\n", i);

			// close_uv_listeners adds the number of additional uv_close calls to
			// running_cleanup_cbs, so set that to 0
			rctx->running_cleanup_cbs = 0;
			close_uv_listeners(rctx);
			uv_run(&rctx->loop, UV_RUN_DEFAULT);
			// can't close the loop since h2o registers some handles to the uv loop

			ret = -5;
			goto cleanup;
		}

		if (create_signal_handler(rctx, SIGTERM) < 0) {
			H2OW_ERR("Failed to register SIGTERM handler for thread %d\n", i);

			// close_uv_listeners adds the number of additional uv_close calls to
			// running_cleanup_cbs, so set that to 1 (for the SIGINT handler)
			rctx->running_cleanup_cbs = 1;
			close_uv_listeners(rctx);
			uv_close((uv_handle_t*)&rctx->int_handler, h2ow__cleanup_cb);
			uv_run(&rctx->loop, UV_RUN_DEFAULT);
			// can't close the loop since h2o registers some handles to the uv loop

			ret = -5;
			goto cleanup;
		}

		// in case we error out during initialization, use cleanup_until to tell
		// later parts or the code how many thread contexts have been fully initialized
		cleanup_until = i;
	}

	run_all_threads(wctx, thread_infos);

cleanup:
	for (int i = 0; i <= cleanup_until; i++) {
		// we can't do much here, since we can't call uv_loop_close
		// because h2o registers some timers and doesn't bother to support
		// cleaning them up. (trying to clean up libh2o will cause an abort)
		// github.com/kazuho plis
		h2ow_run_context* rctx = &wctx->run_contexts[i];
		delete_handler(rctx);
	}

	// only clean up ssl context if we created it
	if (settings->ssl_ctx == NULL && wctx->ssl_ctx != NULL)
		SSL_CTX_free(wctx->ssl_ctx);

	free(wctx->run_contexts);
	free(wctx->threads);

	return ret;
}
