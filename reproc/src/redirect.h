#pragma once

#include "handle.h"
#include "pipe.h"

// Keep in sync with `REPROC_STREAM`.
typedef enum {
  REDIRECT_STREAM_IN,
  REDIRECT_STREAM_OUT,
  REDIRECT_STREAM_ERR
} REDIRECT_STREAM;

int redirect_parent(handle_type *out, REDIRECT_STREAM stream);

int redirect_discard(handle_type *out, REDIRECT_STREAM stream);
