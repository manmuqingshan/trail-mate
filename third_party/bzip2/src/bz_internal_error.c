/*
 * Host implementation required by libbzip2 when BZ_NO_STDIO is enabled.
 * Normal decompression errors are returned through the public API; this hook is
 * only reached for internal consistency assertions.
 */

#include "bzlib_private.h"

#include <stdlib.h>

void bz_internal_error(int errcode)
{
    (void)errcode;
    abort();
}
