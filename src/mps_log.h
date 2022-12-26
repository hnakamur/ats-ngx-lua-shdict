#ifndef _MPS_LOG_H_INCLUDED_
#define _MPS_LOG_H_INCLUDED_

#include <inttypes.h>

#if MPS_NGX

#include "tslog_ngx.h"
#define LogLenStr "%*s"

#elif MPS_ATS

#include "tslog.h"
#define LogLenStr "%.*s"

#else

#include "tslog_stderr.h"
#define LogLenStr "%.*s"

#endif

#define MPS_LOG_TAG "mps_shdict"

#endif /* _MPS_LOG_H_INCLUDED_ */
