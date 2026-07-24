#pragma once

#include <cstdint>

#include "xsched/lg200/level2.h"
#include "xsched/types.h"

namespace xsched::lg200
{

struct KcdQueueControl;
using KcdIoctlFunction = int (*)(int, unsigned long, ...);

XResult KcdQueueCaptureBegin();
XResult KcdQueueCaptureCancel();
XResult KcdQueueCaptureEnd(KcdQueueControl **control);

void KcdQueueControlDestroy(KcdQueueControl *control);
XResult KcdQueueDeactivate(KcdQueueControl *control);
XResult KcdQueueReactivate(KcdQueueControl *control);
XResult KcdQueueControlGetStats(const KcdQueueControl *control,
    Lg200KcdQueueStats *stats);

#if defined(XSCHED_LG200_KCD_TEST)
void KcdQueueSetIoctlForTest(KcdIoctlFunction function);
#endif

} // namespace xsched::lg200
