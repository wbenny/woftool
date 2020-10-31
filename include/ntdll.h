#pragma once

//
// Remap definitions.
//

#ifdef NTDLL_NO_INLINE_INIT_STRING
#define PHNT_NO_INLINE_INIT_STRING
#endif

//
// Hack, because prototype in PH's headers and evntprov.h
// don't match.
//

#define EtwEventRegister __EtwEventRegisterIgnored

#include "phnt/phnt_windows.h"
#include "phnt/phnt.h"

#undef  EtwEventRegister

//
// Include support for ETW logging.
// Note that following functions are mocked, because they're
// located in advapi32.dll.  Fortunatelly, advapi32.dll simply
// redirects calls to these functions to the ntdll.dll.
//

#define EventActivityIdControl  EtwEventActivityIdControl
#define EventEnabled            EtwEventEnabled
#define EventProviderEnabled    EtwEventProviderEnabled
#define EventRegister           EtwEventRegister
#define EventSetInformation     EtwEventSetInformation
#define EventUnregister         EtwEventUnregister
#define EventWrite              EtwEventWrite
#define EventWriteEndScenario   EtwEventWriteEndScenario
#define EventWriteEx            EtwEventWriteEx
#define EventWriteStartScenario EtwEventWriteStartScenario
#define EventWriteString        EtwEventWriteString
#define EventWriteTransfer      EtwEventWriteTransfer

#include <evntprov.h>
