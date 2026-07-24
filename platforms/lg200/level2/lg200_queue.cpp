#include "lg200_queue.h"

#include <memory>

#include "xsched/opencl/hal/types.h"
#include "xsched/preempt/hal/hw_queue.h"
#include "xsched/utils/xassert.h"

using namespace xsched::lg200;
using namespace xsched::opencl;
using namespace xsched::preempt;

Lg200OclQueue::Lg200OclQueue(cl_command_queue cmdq,
    KcdQueueControl *control)
    : OclQueue(cmdq)
    , control_(control, KcdQueueControlDestroy)
{
    XASSERT(control != nullptr, "LG200 KCD queue control is null");
}

void Lg200OclQueue::Deactivate()
{
    const XResult result = KcdQueueDeactivate(control_.get());
    XASSERT(result == kXSchedSuccess,
        "failed to deactivate LG200 KCD queue group: %d", result);
}

void Lg200OclQueue::Reactivate(const preempt::CommandLog &)
{
    const XResult result = KcdQueueReactivate(control_.get());
    XASSERT(result == kXSchedSuccess,
        "failed to reactivate LG200 KCD queue group: %d", result);
}

void Lg200OclQueue::OnHwCommandSubmit(std::shared_ptr<HwCommand> hw_cmd)
{
    XASSERT(hw_cmd != nullptr, "LG200 HwCommand is null");
    hw_cmd->SetProps(kCommandPropertyDeactivatable);
}

XResult Lg200OclQueue::GetControlStats(Lg200KcdQueueStats *stats) const
{
    return KcdQueueControlGetStats(control_.get(), stats);
}

EXPORT_C_FUNC XResult Lg200KcdQueueCaptureBegin(void)
{
    return KcdQueueCaptureBegin();
}

EXPORT_C_FUNC XResult Lg200KcdQueueCaptureCancel(void)
{
    return KcdQueueCaptureCancel();
}

EXPORT_C_FUNC XResult Lg200OclQueueCreate(HwQueueHandle *hwq,
    cl_command_queue cmdq)
{
    if (hwq == nullptr || cmdq == nullptr) return kXSchedErrorInvalidValue;

    KcdQueueControl *control = nullptr;
    XResult result = KcdQueueCaptureEnd(&control);
    if (result != kXSchedSuccess) return result;

    const HwQueueHandle handle = GetHwQueueHandle(cmdq);
    if (HwQueueManager::Get(handle) != nullptr) {
        KcdQueueControlDestroy(control);
        return kXSchedErrorNotAllowed;
    }

    auto queue = std::make_shared<Lg200OclQueue>(cmdq, control);
    result = HwQueueManager::Add(handle, [queue]() { return queue; });
    if (result == kXSchedSuccess) *hwq = handle;
    return result;
}

EXPORT_C_FUNC XResult Lg200KcdQueueGetStats(HwQueueHandle hwq,
    Lg200KcdQueueStats *stats)
{
    if (stats == nullptr) return kXSchedErrorInvalidValue;

    auto queue = std::dynamic_pointer_cast<Lg200OclQueue>(HwQueueManager::Get(hwq));
    if (queue == nullptr) return kXSchedErrorNotFound;
    return queue->GetControlStats(stats);
}
