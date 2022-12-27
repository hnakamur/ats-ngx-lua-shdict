#ifndef _MPS_LOG_H_INCLUDED_
#define _MPS_LOG_H_INCLUDED_

#include <inttypes.h>

#if MPS_NGX

#include "tslog_ngx.h"
#define LogLenStr "%*s"

#elif MPS_ATS

#include "tslog.h"
#define LogLenStr "%.*s"

#elif MPS_LOG_NOP

#define TSStatus(...)
#define TSNote(...)
#define TSWarning(...)
#define TSError(...)
#define TSFatal(...)
#define TSAlert(...)
#define TSEmergency(...)
#define TSDebug(...)
#define LogLenStr

#else

#include "tslog_stderr.h"
#define LogLenStr "%.*s"

#endif

#define MPS_LOG_TAG "mps_shdict"

#endif /* _MPS_LOG_H_INCLUDED_ */
