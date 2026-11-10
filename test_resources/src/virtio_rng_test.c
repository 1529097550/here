#include "io.h"
#include "power.h"
#include "log.h"
#include "virtio/virtio_mmio.h"
#include "virtio/virtio_queue.h"
#include <stdint.h>

#define QUEUE_SIZE 8
#define AVAIL_RING_SIZE (sizeof(uint16_t) * QUEUE_SIZE + sizeof(VirtQueueAvail))
#define USED_RING_SIZE (sizeof(VirtQueueUsedElem) * QUEUE_SIZE + sizeof(VirtQueueUsed))
#define DESC_SIZE 16
#define RNG_BUF_LEN 32

struct VirtIOMMIOLayout {
    uint32_t magic_value;           // 0x000
    uint32_t version;               // 0x004
    uint32_t device_id;             // 0x008
    uint32_t vendor_id;             // 0x00c
    uint32_t device_features;       // 0x010
    uint32_t device_features_sel;   // 0x014
    uint32_t reserved_0[2];         // 0x018, 0x01c
    uint32_t driver_features;       // 0x020
    uint32_t driver_features_sel;   // 0x024
    uint32_t reserved_1[2];         // 0x028, 0x02c
    uint32_t queue_sel;             // 0x030
    uint32_t queue_num_max;         // 0x034
    uint32_t queue_num;             // 0x038
    uint32_t queue_align;           // 0x03c
    uint32_t queue_pfn;             // 0x040
    uint32_t queue_ready;           // 0x044
    uint32_t reserved_2[2];         // 0x048, 0x04c
    uint32_t queue_notify;          // 0x050
    uint32_t reserved_3[3];         // 0x054, 0x058, 0x05c
    uint32_t interrupt_status;      // 0x060
    uint32_t interrupt_ack;         // 0x064
    uint32_t reserved_4[2];         // 0x068, 0x06c
    uint32_t status;                // 0x070
    uint32_t reserved_5[3];         // 0x074, 0x078, 0x07c
    uint32_t queue_desc_low;        // 0x080
    uint32_t queue_desc_high;       // 0x084
    uint32_t reserved_6[2];         // 0x088, 0x08c
    uint32_t queue_avail_low;       // 0x090
    uint32_t queue_avail_high;      // 0x094
    uint32_t reserved_7[2];         // 0x098, 0x09c
    uint32_t queue_used_low;        // 0x0A0
    uint32_t queue_used_high;       // 0x0A4
    uint32_t reserved_8[22];        // 22 words, 0x0A8 ~ 0x0FC
    uint8_t  config_space[];        // 0x100
};

// 第一个 virtio-mmio 槽位（模拟器按注册顺序分配 0x10001000 + n*0x1000）
#define VIRTIO_MMIO_BASE 0x10001000
volatile struct VirtIOMMIOLayout *rng = (volatile struct VirtIOMMIOLayout *)VIRTIO_MMIO_BASE;

uint8_t rng_buf[RNG_BUF_LEN] __attribute__((aligned(4096)));
VirtQueueDesc desc[DESC_SIZE];
uint8_t avail_base[AVAIL_RING_SIZE];
VirtQueueAvail* avail = (VirtQueueAvail*)avail_base;
uint8_t used_base[USED_RING_SIZE];
VirtQueueUsed* used = (VirtQueueUsed*)used_base;

// 提交一个单描述符链（设备可写缓冲区）请求并等待完成
static void submit_rng_request(uint16_t* avail_idx, uint16_t desc_idx) {
    // 填充毒值，验证设备确实写入了新数据
    for (int i = 0; i < RNG_BUF_LEN; i++) {
        rng_buf[i] = 0;
    }

    VirtQueueDesc* d = &desc[desc_idx];
    d->paddr = (uint64_t)(rng_buf);
    d->len = RNG_BUF_LEN;
    d->flags = VIRTQ_DESC_F_WRITE; // device-writable
    d->next = 0;

    avail->ring[0] = desc_idx;
    avail->idx = ++(*avail_idx);

    rng->queue_notify = 0;

    // RNG 设备同步完成请求：used ring 与中断应在 notify 后立即可见
    if (!(rng->interrupt_status & VIRTIO_MMIO_INT_VRING)) {
        Log(ERROR, "Missing used-ring interrupt\n");
        fail();
    }
    rng->interrupt_ack = VIRTIO_MMIO_INT_VRING;
    if (rng->interrupt_status & VIRTIO_MMIO_INT_VRING) {
        Log(ERROR, "Interrupt not deasserted after ack\n");
        fail();
    }
}

int main() {
    TEST_START(__func__);

    // 探测设备：Magic = "virt", Version = 2, DeviceID = 4 (entropy source)
    if (rng->magic_value != 0x74726976) {
        Log(ERROR, "Bad magic value: 0x%x\n", rng->magic_value);
        fail();
    }
    if (rng->version != 2) {
        Log(ERROR, "Bad virtio version: %d\n", rng->version);
        fail();
    }
    if (rng->device_id != 4) {
        Log(ERROR, "Bad device id: %d (expect 4 for virtio-rng)\n", rng->device_id);
        fail();
    }

    // 复位后按协议驱动设备
    rng->status = 0;
    rng->status = ACKNOWLEDGE;
    rng->status = ACKNOWLEDGE | DRIVER;

    // 协商 VERSION_1（feature bit 32）
    rng->device_features_sel = 0;
    uint32_t host_features_lo = rng->device_features;
    rng->device_features_sel = 1;
    uint32_t host_features_hi = rng->device_features;
    if (!(host_features_hi & 0x1)) {
        Log(ERROR, "Device does not offer VERSION_1 (hi=0x%x)\n", host_features_hi);
        fail();
    }
    rng->driver_features_sel = 0;
    rng->driver_features = host_features_lo;
    rng->driver_features_sel = 1;
    rng->driver_features = host_features_hi;

    rng->status = ACKNOWLEDGE | DRIVER | FEATURES_OK;
    if (!(rng->status & FEATURES_OK)) {
        Log(ERROR, "Feature negotiation failed\n");
        fail();
    }

    // 设置队列
    rng->queue_sel = 0;
    if (rng->queue_num_max == 0) {
        Log(ERROR, "No queue 0\n");
        fail();
    }
    rng->queue_num = QUEUE_SIZE;

    rng->queue_desc_low = (uint64_t)(desc) & 0xffffffff;
    rng->queue_desc_high = ((uint64_t)(desc) >> 32) & 0xffffffff;
    rng->queue_avail_low = (uint64_t)(avail) & 0xffffffff;
    rng->queue_avail_high = ((uint64_t)(avail) >> 32) & 0xffffffff;
    rng->queue_used_low = (uint64_t)(used) & 0xffffffff;
    rng->queue_used_high = ((uint64_t)(used) >> 32) & 0xffffffff;

    rng->queue_ready = 1;

    avail->flags = 0;
    uint16_t avail_idx = 0;
    avail->idx = avail_idx;
    used->flags = 0;
    used->idx = 0;

    // 第一次请求
    submit_rng_request(&avail_idx, 0);
    while (used->idx != 1) {
        // 同步设备下不会发生，防御性轮询
    }
    if (used->ring[0].id != 0) {
        Log(ERROR, "Bad used id: %d\n", used->ring[0].id);
        fail();
    }
    if (used->ring[0].len != RNG_BUF_LEN) {
        Log(ERROR, "Bad used len: %d (expect %d)\n", used->ring[0].len, RNG_BUF_LEN);
        fail();
    }

    uint8_t first[RNG_BUF_LEN];
    for (int i = 0; i < RNG_BUF_LEN; i++) {
        first[i] = rng_buf[i];
    }

    // 第二次请求，结果必须与第一次不同
    submit_rng_request(&avail_idx, 1);
    while (used->idx != 2) {
    }
    if (used->ring[1].len != RNG_BUF_LEN) {
        Log(ERROR, "Bad used len on second request: %d\n", used->ring[1].len);
        fail();
    }

    int same = 1;
    for (int i = 0; i < RNG_BUF_LEN; i++) {
        if (first[i] != rng_buf[i]) {
            same = 0;
            break;
        }
    }
    if (same) {
        Log(ERROR, "Two RNG requests returned identical bytes\n");
        fail();
    }

    // 打印两次随机数
    printf("rng #1: ");
    for (int i = 0; i < RNG_BUF_LEN; i++) {
        printf("%02x", first[i]);
    }
    printf("\r\n");
    printf("rng #2: ");
    for (int i = 0; i < RNG_BUF_LEN; i++) {
        printf("%02x", rng_buf[i]);
    }
    printf("\r\n");

    pass();
    return 0;
}
