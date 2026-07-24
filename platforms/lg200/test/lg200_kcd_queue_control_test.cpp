#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../level2/kcd_queue_control.h"

namespace
{

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

static_assert(sizeof(KcdCreateQueueArgs) == 72);
static_assert(sizeof(KcdUpdateQueueArgs) == 24);

constexpr unsigned long kKcdCreateQueue = _IOWR('K', 0x02, KcdCreateQueueArgs);
constexpr unsigned long kKcdUpdateQueue = _IOW('K', 0x07, KcdUpdateQueueArgs);

struct FakeIoctlState
{
    int call_count = 0;
    int fail_call = -1;
    std::vector<KcdUpdateQueueArgs> updates;
};

FakeIoctlState g_ioctl;

int FakeIoctl(int, unsigned long request, ...)
{
    void *argument = nullptr;
    va_list arguments;
    va_start(arguments, request);
    argument = va_arg(arguments, void *);
    va_end(arguments);

    if (request == kKcdUpdateQueue && argument != nullptr) {
        g_ioctl.updates.push_back(
            *static_cast<KcdUpdateQueueArgs *>(argument));
    }

    ++g_ioctl.call_count;
    if (g_ioctl.call_count == g_ioctl.fail_call) {
        errno = EIO;
        return -1;
    }
    return 0;
}

bool Expect(bool condition, const std::string &message)
{
    if (condition) return true;
    std::cerr << "FAIL: " << message << '\n';
    return false;
}

bool CaptureQueue(int fd, uint32_t queue_id, uint32_t percentage)
{
    KcdCreateQueueArgs queue {};
    queue.ring_base_address = 0x100000 + queue_id * 0x1000;
    queue.ring_size = 4096;
    queue.gpu_id = 7;
    queue.queue_type = 0;
    queue.queue_percentage = percentage;
    queue.queue_priority = 1;
    queue.queue_id = queue_id;
    return ioctl(fd, kKcdCreateQueue, &queue) == 0;
}

bool TestBalancedGroupControl(int fd)
{
    using namespace xsched::lg200;

    bool ok = Expect(KcdQueueCaptureBegin() == kXSchedSuccess,
        "begin queue capture");
    ok &= Expect(CaptureQueue(fd, 10, 100), "capture first active queue");
    ok &= Expect(CaptureQueue(fd, 11, 50), "capture second active queue");
    ok &= Expect(CaptureQueue(fd, 12, 0), "capture inactive queue");

    KcdQueueControl *control = nullptr;
    ok &= Expect(KcdQueueCaptureEnd(&control) == kXSchedSuccess,
        "finish queue capture");
    if (!ok || control == nullptr) return false;

    Lg200KcdQueueStats stats {};
    ok &= Expect(KcdQueueControlGetStats(control, &stats) == kXSchedSuccess,
        "read initial stats");
    ok &= Expect(stats.gpu_id == 7, "wrong GPU id");
    ok &= Expect(stats.native_queue_count == 3,
        "wrong native queue count");
    ok &= Expect(stats.active_native_queue_count == 2,
        "wrong active native queue count");

    g_ioctl = {};
    ok &= Expect(KcdQueueDeactivate(control) == kXSchedSuccess,
        "deactivate queue group");
    ok &= Expect(g_ioctl.updates.size() == 2,
        "deactivation must update only active queues");
    if (g_ioctl.updates.size() == 2) {
        ok &= Expect(g_ioctl.updates[0].queue_percentage == 0 && g_ioctl.updates[1].queue_percentage == 0,
            "deactivation must write zero percentages");
    }

    g_ioctl = {};
    ok &= Expect(KcdQueueReactivate(control) == kXSchedSuccess,
        "reactivate queue group");
    ok &= Expect(g_ioctl.updates.size() == 2,
        "reactivation update count");
    if (g_ioctl.updates.size() == 2) {
        ok &= Expect(g_ioctl.updates[0].queue_percentage == 100 && g_ioctl.updates[1].queue_percentage == 50,
            "reactivation must restore original percentages");
    }

    ok &= Expect(KcdQueueControlGetStats(control, &stats) == kXSchedSuccess,
        "read final stats");
    ok &= Expect(stats.deactivate_count == 1 && stats.reactivate_count == 1,
        "logical control counts must balance");
    ok &= Expect(stats.deactivate_update_count == 2 && stats.reactivate_update_count == 2,
        "native update counts must balance");

    KcdQueueControlDestroy(control);
    return ok;
}

bool TestRollback(int fd)
{
    using namespace xsched::lg200;

    bool ok = Expect(KcdQueueCaptureBegin() == kXSchedSuccess,
        "begin rollback capture");
    ok &= Expect(CaptureQueue(fd, 20, 80), "capture rollback queue one");
    ok &= Expect(CaptureQueue(fd, 21, 40), "capture rollback queue two");

    KcdQueueControl *control = nullptr;
    ok &= Expect(KcdQueueCaptureEnd(&control) == kXSchedSuccess,
        "finish rollback capture");
    if (!ok || control == nullptr) return false;

    g_ioctl = {};
    g_ioctl.fail_call = 2;
    ok &= Expect(KcdQueueDeactivate(control) == kXSchedErrorHardware,
        "partial failure must be reported");
    ok &= Expect(g_ioctl.updates.size() == 3,
        "partial failure must roll back changed members");
    if (g_ioctl.updates.size() == 3) {
        ok &= Expect(g_ioctl.updates.back().queue_id == 20 && g_ioctl.updates.back().queue_percentage == 80,
            "rollback must restore the first member");
    }

    Lg200KcdQueueStats stats {};
    ok &= Expect(KcdQueueControlGetStats(control, &stats) == kXSchedSuccess,
        "rolled-back control remains consistent");
    ok &= Expect(stats.deactivate_count == 0 && stats.deactivate_update_count == 0,
        "failed transaction must not increment proof counters");

    g_ioctl = {};
    KcdQueueControlDestroy(control);
    return ok;
}

} // namespace

int main()
{
    using namespace xsched::lg200;

    KcdQueueSetIoctlForTest(FakeIoctl);
    const int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        std::cerr << "FAIL: open /dev/null\n";
        return 1;
    }

    bool ok = TestBalancedGroupControl(fd) && TestRollback(fd);
    ok &= Expect(KcdQueueCaptureBegin() == kXSchedSuccess,
        "begin cancellable capture");
    ok &= Expect(KcdQueueCaptureCancel() == kXSchedSuccess,
        "cancel capture");
    ok &= Expect(KcdQueueCaptureCancel() == kXSchedErrorNotAllowed,
        "reject duplicate cancel");

    close(fd);
    KcdQueueSetIoctlForTest(nullptr);
    if (!ok) return 1;
    std::cout << "LG200 KCD queue-control tests passed.\n";
    return 0;
}
