#pragma once

#include <stdint.h>

#include "xsched/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _cl_command_queue *cl_command_queue;

typedef struct
{
    uint32_t gpu_id;
    uint32_t native_queue_count;
    uint32_t active_native_queue_count;
    uint64_t deactivate_count;
    uint64_t reactivate_count;
    uint64_t deactivate_update_count;
    uint64_t reactivate_update_count;
} Lg200KcdQueueStats;

// Begin a process-wide capture window immediately before creating one LACm
// OpenCL command queue. Concurrent capture windows are rejected.
XResult Lg200KcdQueueCaptureBegin(void);

// Cancel the active capture window after native queue creation fails.
XResult Lg200KcdQueueCaptureCancel(void);

// Consume the active capture window and create a concrete Level-2 HwQueue for
// the supplied LACm OpenCL queue. The queue uses KCD UPDATE_QUEUE to remove and
// restore every captured compute queue from the hardware runlist.
XResult Lg200OclQueueCreate(HwQueueHandle *hwq, cl_command_queue cmdq);

XResult Lg200KcdQueueGetStats(HwQueueHandle hwq,
    Lg200KcdQueueStats *stats);

#ifdef __cplusplus
}
#endif
