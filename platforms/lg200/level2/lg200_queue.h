#pragma once

#include <memory>

#include "kcd_queue_control.h"
#include "xsched/lg200/level2.h"
#include "xsched/opencl/hal/ocl_queue.h"

namespace xsched::lg200
{

class Lg200OclQueue final : public opencl::OclQueue
{
public:
    Lg200OclQueue(cl_command_queue cmdq, KcdQueueControl *control);
    virtual ~Lg200OclQueue() = default;

    virtual void Deactivate() override;
    virtual void Reactivate(const preempt::CommandLog &log) override;
    virtual void OnHwCommandSubmit(
        std::shared_ptr<preempt::HwCommand> hw_cmd) override;

    virtual bool SupportDynamicLevel() override { return true; }
    virtual XPreemptLevel GetMaxSupportedLevel() override
    {
        return kPreemptLevelDeactivate;
    }

    XResult GetControlStats(Lg200KcdQueueStats *stats) const;

private:
    std::unique_ptr<KcdQueueControl, void (*)(KcdQueueControl *)> control_;
};

} // namespace xsched::lg200
