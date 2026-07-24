# Loongson LG200 device discovery

This module discovers LG200 DRM devices owned by the `loonggpu` kernel driver
and reads optional utilization and local-memory attributes exported through
sysfs.

The base module intentionally contains device discovery only. When
`LG200_KCD_LEVEL2` is enabled, it also builds a hardware-specific OpenCL
`HwQueue` that deactivates and reactivates LoongGPU KCD compute queues.
Commands continue to use XSched's existing OpenCL HAL or
`HwCommandCreateCallback`; this adapter does not duplicate a generic command
wrapper.

## Build

```shell
cmake -S . -B build-lg200 \
  -DPLATFORM_LG200=ON \
  -DPLATFORM_OPENCL=ON \
  -DLG200_KCD_LEVEL2=ON \
  -DBUILD_TEST=ON
cmake --build build-lg200 --target \
  lg200_info \
  lg200_resource_test \
  lg200_kcd_queue_control_test \
  lg200_level2_contract_test
```

Run `lg200_info --require-device` on a Loongson system to require at least one
matching DRM device.

## API

- `Lg200DeviceEnumerate()` enumerates `cardN` nodes whose driver name starts
  with `loonggpu` or whose product identity contains `LG200`.
- `Lg200DeviceReadStats()` reads `gpu_busy_percent`,
  `mem_info_vram_used`, and `mem_info_vram_total` when the driver exports them.

Statistics are optional. An unavailable utilization value is returned as
`-1.0`; unavailable memory counters are returned as zero.

## KCD Level-2 queue control

LACm may create more than one KCD compute queue for one OpenCL command queue.
The adapter captures the complete native group during
`clCreateCommandQueue()`, duplicates the KCD file descriptors, and stores every
member's original scheduling percentage.

`Lg200OclQueue::Deactivate()` sends
`LGKCD_IOC_UPDATE_QUEUE(queue_percentage=0)` for each active member.
`Reactivate()` restores the original non-zero percentages. A partial update is
rolled back in reverse order; a rollback failure permanently marks the group
inconsistent and aborts further control. Inactive members are never
accidentally activated.

```c
Lg200KcdQueueCaptureBegin();
cl_command_queue cmdq = clCreateCommandQueue(context, device, 0, &err);
if (cmdq == NULL) {
    Lg200KcdQueueCaptureCancel();
}

HwQueueHandle hwq = 0;
Lg200OclQueueCreate(&hwq, cmdq);
XQueueCreate(&xqueue, hwq, kPreemptLevelDeactivate,
             kQueueCreateFlagNone);
```

The capture window is process-wide and must surround exactly one LACm queue
creation. Link or preload `hallg200` before the vendor OpenCL runtime so its
`ioctl` interposer can observe the KCD create requests.

This is XSched Level-2: it removes already-submitted dispatches from the
hardware runlist at dispatch boundaries. It does not interrupt a kernel that is
already executing and does not claim Level-3 context save/restore.

The ABI declarations match the `LGKCD_IOC_CREATE_QUEUE` and
`LGKCD_IOC_UPDATE_QUEUE` UAPI shipped in Loongson's
`loonggpu-kernel-dkms 1.0.2` package. The adapter does not inspect private LACm
object layouts and fails closed if the expected queue group cannot be captured.
