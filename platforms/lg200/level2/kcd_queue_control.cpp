#include "kcd_queue_control.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <mutex>
#include <new>
#include <vector>

#if defined(__linux__)
#include <cstdarg>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace xsched::lg200
{

namespace
{

#if defined(__linux__)

    constexpr uint32_t kKcdQueueTypeCompute = 0;

    struct KcdCreateQueueArgs
    {
        uint64_t ring_base_address;
        uint64_t write_pointer_address;
        uint64_t read_pointer_address;
        uint64_t doorbell_offset;
        uint32_t ring_size;
        uint32_t gpu_id;
        uint32_t queue_type;
        uint32_t queue_percentage;
        uint32_t queue_priority;
        uint32_t queue_id;
        uint64_t ctx_save_restore_address;
        uint32_t ctx_save_restore_size;
        uint32_t pad;
    };

    struct KcdUpdateQueueArgs
    {
        uint64_t ring_base_address;
        uint32_t queue_id;
        uint32_t ring_size;
        uint32_t queue_percentage;
        uint32_t queue_priority;
    };

    static_assert(sizeof(KcdCreateQueueArgs) == 72, "unexpected KCD create ABI");
    static_assert(sizeof(KcdUpdateQueueArgs) == 24, "unexpected KCD update ABI");

    constexpr unsigned long kKcdCreateQueue = _IOWR('K', 0x02, KcdCreateQueueArgs);
    constexpr unsigned long kKcdUpdateQueue = _IOW('K', 0x07, KcdUpdateQueueArgs);

    struct CapturedQueue
    {
        int fd = -1;
        KcdCreateQueueArgs args {};
    };

    struct ControlledQueue
    {
        int fd = -1;
        KcdCreateQueueArgs create {};
        uint32_t current_percentage = 0;
    };

    struct CaptureState
    {
        std::mutex mutex;
        bool active = false;
        std::vector<CapturedQueue> queues;
    };

    CaptureState &GetCaptureState()
    {
        static CaptureState state;
        return state;
    }

#if defined(XSCHED_LG200_KCD_TEST)
    KcdIoctlFunction &TestIoctl()
    {
        static KcdIoctlFunction function = nullptr;
        return function;
    }
#endif

    KcdIoctlFunction ResolveIoctl()
    {
#if defined(XSCHED_LG200_KCD_TEST)
        if (TestIoctl() != nullptr) return TestIoctl();
#endif
        static KcdIoctlFunction function = reinterpret_cast<KcdIoctlFunction>(dlsym(RTLD_NEXT, "ioctl"));
        return function;
    }

    XResult ResultFromErrno(int error)
    {
        switch (error) {
        case EACCES:
        case EPERM:
            return kXSchedErrorNotAllowed;
        case ENODEV:
        case ENOENT:
        case ENOSYS:
        case ENOTTY:
        case EOPNOTSUPP:
            return kXSchedErrorNotSupported;
        case EINVAL:
            return kXSchedErrorInvalidValue;
        default:
            return kXSchedErrorHardware;
        }
    }

#endif

} // namespace

struct KcdQueueControl
{
#if defined(__linux__)
    std::vector<ControlledQueue> queues;
    mutable std::mutex mutex;
    bool active = true;
    bool consistent = true;
    std::atomic<uint64_t> deactivate_count { 0 };
    std::atomic<uint64_t> reactivate_count { 0 };
    std::atomic<uint64_t> deactivate_update_count { 0 };
    std::atomic<uint64_t> reactivate_update_count { 0 };
#endif
};

#if defined(__linux__)

// LACm creates native compute queues through this libc entry point. Linking or
// preloading hallg200 before the vendor OpenCL runtime lets the adapter observe
// the documented KCD UAPI without inspecting cl_command_queue object layouts.
extern "C" int ioctl(int fd, unsigned long request, ...) noexcept
{
    void *argument = nullptr;
    va_list arguments;
    va_start(arguments, request);
    argument = va_arg(arguments, void *);
    va_end(arguments);

    const KcdIoctlFunction real_ioctl = ResolveIoctl();
    if (real_ioctl == nullptr) {
        errno = ENOSYS;
        return -1;
    }

    const int result = real_ioctl(fd, request, argument);
    if (result == 0 && request == kKcdCreateQueue && argument != nullptr) {
        const auto *created = static_cast<const KcdCreateQueueArgs *>(argument);
        if (created->queue_type == kKcdQueueTypeCompute) {
            CaptureState &capture = GetCaptureState();
            std::lock_guard<std::mutex> lock(capture.mutex);
            if (!capture.active) return result;
            try {
                capture.queues.push_back(CapturedQueue { fd, *created });
            } catch (...) {
                // Queue creation already succeeded. CaptureEnd fails closed
                // rather than returning a partially captured native group.
                capture.queues.clear();
                capture.active = false;
            }
        }
    }
    return result;
}

#endif

XResult KcdQueueCaptureBegin(void)
{
#if defined(__linux__)
    CaptureState &capture = GetCaptureState();
    std::lock_guard<std::mutex> lock(capture.mutex);
    if (capture.active) return kXSchedErrorNotAllowed;
    capture.queues.clear();
    capture.active = true;
    return kXSchedSuccess;
#else
    return kXSchedErrorNotSupported;
#endif
}

XResult KcdQueueCaptureCancel(void)
{
#if defined(__linux__)
    CaptureState &capture = GetCaptureState();
    std::lock_guard<std::mutex> lock(capture.mutex);
    if (!capture.active) return kXSchedErrorNotAllowed;
    capture.queues.clear();
    capture.active = false;
    return kXSchedSuccess;
#else
    return kXSchedErrorNotSupported;
#endif
}

XResult KcdQueueCaptureEnd(KcdQueueControl **control)
{
    if (control == nullptr) return kXSchedErrorInvalidValue;
    *control = nullptr;

#if defined(__linux__)
    CaptureState &capture = GetCaptureState();
    std::vector<CapturedQueue> captured;
    {
        std::lock_guard<std::mutex> lock(capture.mutex);
        if (!capture.active) return kXSchedErrorNotAllowed;
        capture.active = false;
        if (capture.queues.empty()) return kXSchedErrorNotFound;
        captured = std::move(capture.queues);
    }

    auto *created = new (std::nothrow) KcdQueueControl;
    if (created == nullptr) return kXSchedErrorUnknown;
    try {
        created->queues.reserve(captured.size());
    } catch (...) {
        delete created;
        return kXSchedErrorUnknown;
    }

    bool has_active_queue = false;
    const uint32_t gpu_id = captured.front().args.gpu_id;
    for (const CapturedQueue &queue : captured) {
        if (queue.args.gpu_id != gpu_id) {
            for (const ControlledQueue &owned : created->queues) close(owned.fd);
            delete created;
            return kXSchedErrorBadResponse;
        }
        const int owned_fd = fcntl(queue.fd, F_DUPFD_CLOEXEC, 0);
        if (owned_fd < 0) {
            const XResult result = ResultFromErrno(errno);
            for (const ControlledQueue &owned : created->queues) close(owned.fd);
            delete created;
            return result;
        }
        created->queues.push_back(
            ControlledQueue { owned_fd, queue.args, queue.args.queue_percentage });
        has_active_queue = has_active_queue || queue.args.queue_percentage != 0;
    }
    if (!has_active_queue) {
        for (const ControlledQueue &owned : created->queues) close(owned.fd);
        delete created;
        return kXSchedErrorBadResponse;
    }
    *control = created;
    return kXSchedSuccess;
#else
    return kXSchedErrorNotSupported;
#endif
}

void KcdQueueControlDestroy(KcdQueueControl *control)
{
    if (control == nullptr) return;
#if defined(__linux__)
    if (control->consistent && !control->active) {
        (void)KcdQueueReactivate(control);
    }
    for (const ControlledQueue &queue : control->queues) {
        if (queue.fd >= 0) close(queue.fd);
    }
#endif
    delete control;
}

namespace
{

#if defined(__linux__)
    XResult UpdateQueue(ControlledQueue *queue, uint32_t percentage)
    {
        KcdUpdateQueueArgs update {};
        update.ring_base_address = queue->create.ring_base_address;
        update.queue_id = queue->create.queue_id;
        update.ring_size = queue->create.ring_size;
        update.queue_percentage = percentage;
        update.queue_priority = queue->create.queue_priority;

        const KcdIoctlFunction real_ioctl = ResolveIoctl();
        if (real_ioctl == nullptr) return kXSchedErrorNotSupported;
        if (real_ioctl(queue->fd, kKcdUpdateQueue, &update) != 0) {
            return ResultFromErrno(errno);
        }
        queue->current_percentage = percentage;
        return kXSchedSuccess;
    }

    XResult SetQueueGroupActive(KcdQueueControl *control, bool active)
    {
        if (control == nullptr) return kXSchedErrorInvalidValue;

        std::lock_guard<std::mutex> lock(control->mutex);
        if (!control->consistent) return kXSchedErrorHardware;
        if (control->active == active) return kXSchedSuccess;

        std::vector<size_t> changed;
        try {
            changed.reserve(control->queues.size());
        } catch (...) {
            return kXSchedErrorUnknown;
        }

        for (size_t index = 0; index < control->queues.size(); ++index) {
            ControlledQueue &queue = control->queues[index];
            const uint32_t target = active ? queue.create.queue_percentage : 0;
            if (queue.current_percentage == target) continue;

            const XResult result = UpdateQueue(&queue, target);
            if (result == kXSchedSuccess) {
                changed.push_back(index);
                continue;
            }

            bool rollback_ok = true;
            for (auto it = changed.rbegin(); it != changed.rend(); ++it) {
                ControlledQueue &rollback = control->queues[*it];
                const uint32_t previous = active ? 0 : rollback.create.queue_percentage;
                if (UpdateQueue(&rollback, previous) != kXSchedSuccess) {
                    rollback_ok = false;
                }
            }
            if (!rollback_ok) {
                control->consistent = false;
                return kXSchedErrorHardware;
            }
            return result;
        }

        control->active = active;
        if (active) {
            control->reactivate_count.fetch_add(1, std::memory_order_relaxed);
            control->reactivate_update_count.fetch_add(
                changed.size(), std::memory_order_relaxed);
        } else {
            control->deactivate_count.fetch_add(1, std::memory_order_relaxed);
            control->deactivate_update_count.fetch_add(
                changed.size(), std::memory_order_relaxed);
        }
        return kXSchedSuccess;
    }
#endif

} // namespace

XResult KcdQueueDeactivate(KcdQueueControl *control)
{
#if defined(__linux__)
    return SetQueueGroupActive(control, false);
#else
    (void)control;
    return kXSchedErrorNotSupported;
#endif
}

XResult KcdQueueReactivate(KcdQueueControl *control)
{
#if defined(__linux__)
    return SetQueueGroupActive(control, true);
#else
    (void)control;
    return kXSchedErrorNotSupported;
#endif
}

XResult KcdQueueControlGetStats(
    const KcdQueueControl *control,
    Lg200KcdQueueStats *stats)
{
    if (control == nullptr || stats == nullptr) return kXSchedErrorInvalidValue;
    *stats = {};
#if defined(__linux__)
    std::lock_guard<std::mutex> lock(control->mutex);
    if (!control->consistent || control->queues.empty()) return kXSchedErrorHardware;
    const ControlledQueue &first = control->queues.front();
    stats->gpu_id = first.create.gpu_id;
    stats->native_queue_count = static_cast<uint32_t>(control->queues.size());
    stats->active_native_queue_count = static_cast<uint32_t>(std::count_if(
        control->queues.begin(), control->queues.end(),
        [](const ControlledQueue &queue) {
            return queue.create.queue_percentage != 0;
        }));
    stats->deactivate_count = control->deactivate_count.load(std::memory_order_relaxed);
    stats->reactivate_count = control->reactivate_count.load(std::memory_order_relaxed);
    stats->deactivate_update_count = control->deactivate_update_count.load(std::memory_order_relaxed);
    stats->reactivate_update_count = control->reactivate_update_count.load(std::memory_order_relaxed);
    return kXSchedSuccess;
#else
    return kXSchedErrorNotSupported;
#endif
}

#if defined(XSCHED_LG200_KCD_TEST)
void KcdQueueSetIoctlForTest(KcdIoctlFunction function)
{
#if defined(__linux__)
    TestIoctl() = function;
#else
    (void)function;
#endif
}
#endif

} // namespace xsched::lg200
