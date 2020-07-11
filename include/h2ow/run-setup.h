#ifndef _H2OW_RUN_SETUP_H_INCLUDED
#define _H2OW_RUN_SETUP_H_INCLUDED

#include "defs.h"
#include "runtime.h"

// starts a server using a h2ow_context configured with
// h2ow_set_defaults, h2ow_setopt and h2ow_register_handler
int h2ow_run(h2ow_context* wctx);

#endif
