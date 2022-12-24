#ifndef _MPS_LOG_H_INCLUDED_
#define _MPS_LOG_H_INCLUDED_

#include <inttypes.h>

#if MPS_NGX

#include "tslog_ngx.h"

#else

#include "tslog.h"

#endif

#define MPS_LOG_TAG "mps_shdict"

#endif /* _MPS_LOG_H_INCLUDED_ */
