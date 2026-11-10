use core::slice;
use std::{
    sync::atomic::{AtomicU8, Ordering},
    time::{SystemTime, UNIX_EPOCH},
};

use log::info;
use rand::RngCore;
use rand_chacha::{ChaCha20Rng, rand_core::SeedableRng};

use crate::device::virtio::{
    config::{VirtIOFeatureSet, virtio_reserved_feature},
    virtio_device::VirtIODeviceTrait,
    virtio_mmio::{VIRTIO_DEVICE_ID_RNG, VirtIODeviceStatus},
    virtio_queue::{VirtQueue, VirtQueueDesc},
};

/// VirtIO entropy source device (Device ID 4).
///
/// The device has a single request-only virtqueue with no device
/// configuration space. Each available descriptor chain is scanned in order;
/// every device-writable (`VIRTQ_DESC_F_WRITE`) buffer is filled with fresh
/// random bytes, and the number of written bytes is reported in the used-ring
/// entry. Buffers without the write flag are skipped, matching the virtio-rng
/// spec where the device may only write to device-writable buffers.
///
/// Requests are completed synchronously on the CPU thread: the entropy fill is
/// cheap host-side work (one `fill_bytes` per buffer), so spawning a worker
/// task would add overhead without benefit. The completion publishes the
/// used-ring entry with a `Release` store and then sets the ISR bit with an
/// `AcqRel` RMW, guaranteeing the guest observes fully written buffers once
/// the interrupt is raised.
///
/// The RNG state is a host `StdRng` seeded once per device from the wall
/// clock mixed with the device address, so two devices never share a stream
/// and each request returns different bytes.
pub(crate) struct VirtIORngDevice {
    pub(crate) name: &'static str,
    pub(crate) status: u8,
    pub(crate) isr: AtomicU8,

    host_feature: VirtIOFeatureSet,
    guest_feature: VirtIOFeatureSet,

    pub(crate) generation: u32,
    ram_base_raw: usize,

    rng: ChaCha20Rng,
    queue: VirtQueue,
}

impl VirtIORngDevice {
    pub(crate) fn new(name: &'static str, ram_base_raw: *mut u8) -> Self {
        info!("build virtio rng device.");

        let entropy = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_nanos() as u64)
            .unwrap_or(0);
        let seed = entropy ^ (ram_base_raw as u64).rotate_left(17);
        Self {
            name,
            status: 0,
            isr: AtomicU8::new(0),
            host_feature: virtio_reserved_feature::VERSION_1,
            guest_feature: 0,
            generation: 0,
            ram_base_raw: ram_base_raw as usize,
            rng: ChaCha20Rng::seed_from_u64(seed),
            queue: VirtQueue::new(ram_base_raw, 0), // will be set later
        }
    }

    fn fill_buffer(&mut self, desc: &VirtQueueDesc) -> u32 {
        let buf = unsafe {
            slice::from_raw_parts_mut(
                desc.get_request_package::<u8>(self.ram_base_raw),
                desc.len as usize,
            )
        };
        self.rng.fill_bytes(buf);
        desc.len
    }
}

impl VirtIODeviceTrait for VirtIORngDevice {
    fn get_device_id(&self) -> u16 {
        VIRTIO_DEVICE_ID_RNG
    }

    fn status(&mut self) -> &mut u8 {
        &mut self.status
    }

    fn get_generation(&self) -> u32 {
        self.generation
    }

    fn reset(&mut self) {
        info!("reset virtio rng device.");

        self.status = 0;
        self.guest_feature = 0;
        self.queue.reset();
        self.isr.store(0, Ordering::Release);
    }

    fn isr(&self) -> &AtomicU8 {
        &self.isr
    }

    fn irq_level(&mut self) -> bool {
        self.isr.load(Ordering::Acquire) != 0 && self.queue.interrupts_enabled()
    }

    fn get_host_feature(&self) -> VirtIOFeatureSet {
        self.host_feature
    }

    fn set_feature(&mut self, feature: VirtIOFeatureSet) {
        info!("set virtio rng feature.");
        if feature & virtio_reserved_feature::VERSION_1 == 0
            || self.host_feature & feature != feature
        {
            self.status &= !VirtIODeviceStatus::FEATURES_OK.bits();
        } else {
            self.guest_feature = feature;
        }
    }

    fn set_queue_num(&mut self, num: u32) {
        self.queue.set_queue_num(num);
    }

    fn queue_select(&self, _idx: u32) {
        // ONLY ONE QUEUE.
    }

    fn set_desc(&mut self, addr: u64) {
        self.queue.set_desc(addr);
    }

    fn set_avail(&mut self, addr: u64) {
        self.queue.set_avail(addr);
    }

    fn set_used(&mut self, addr: u64) {
        self.queue.set_used(addr);
    }

    fn manage_one_request(&mut self) -> bool {
        let ram_base_raw = self.ram_base_raw;
        // Fill the shared guest RAM inside the scan callback; the used-ring
        // and ISR updates below publish the data with Release ordering
        // afterwards. The rng is the only self state touched here, so it is
        // taken out of the borrow used by the queue scan.
        let rng = &mut self.rng;
        let mut total_written = 0u32;
        let Some(completion) = self
            .queue
            .take_one_request(|desc: &VirtQueueDesc, _idx: usize| {
                if desc.is_device_writable() {
                    let buf = unsafe {
                        slice::from_raw_parts_mut(
                            desc.get_request_package::<u8>(ram_base_raw),
                            desc.len as usize,
                        )
                    };
                    rng.fill_bytes(buf);
                    total_written += desc.len;
                }
            })
        else {
            return false;
        };
        completion.complete(total_written);
        self.isr.fetch_or(1, Ordering::AcqRel);
        true
    }

    fn notify(&mut self, _idx: u32) {
        loop {
            if !self.manage_one_request() {
                break;
            }
        }
    }

    fn queue_ready(&self) -> bool {
        self.queue.ready()
    }

    fn get_num_of_queue(&self) -> u32 {
        1
    }

    // VirtIO-RNG has no device configuration space.
    fn read_config(&mut self, offset: u64, len: u32) -> u64 {
        info!("virtio-rng config read out of range: offset={offset:#x}, len={len}");
        0
    }

    fn write_config(&mut self, offset: u64, len: u32, data: u64) {
        info!(
            "virtio-rng config write out of range: offset={offset:#x}, len={len}, data={data:#x}"
        );
    }
}

#[cfg(test)]
impl VirtIORngDevice {
    pub(crate) fn queue(&mut self) -> &mut VirtQueue {
        &mut self.queue
    }
}

#[cfg(test)]
mod test {
    use std::sync::atomic::Ordering;

    use crate::{
        device::virtio::virtio_queue::{
            VirtQueueAvail, VirtQueueAvailFlag, VirtQueueDesc, VirtQueueDescFlag, VirtQueueUsed,
            VirtQueueUsedFlag,
        },
        ram::Ram,
        ram_config,
    };

    use super::*;

    const QUEUE_NUM: usize = 8;
    const DESC_TABLE_PADDR: u64 = 0x8000_2000;

    fn setup_queue(ram: &mut Ram, device: &mut VirtIORngDevice) -> (u64, u64, u64) {
        device.set_queue_num(QUEUE_NUM as u32);

        let virtq_desc_base = DESC_TABLE_PADDR;
        let virtq_avail_base = 0x8000_2100 + ((QUEUE_NUM + 2) * size_of::<u16>()) as u64;
        let virtq_used_base = 0x8000_2200 + (QUEUE_NUM * size_of::<VirtQueueUsed>() + 4) as u64;
        device.set_avail(virtq_avail_base);
        device.set_desc(virtq_desc_base);
        device.set_used(virtq_used_base);

        let virtq_avail = &mut ram[(virtq_avail_base - ram_config::BASE_ADDR) as usize] as *mut u8
            as *mut VirtQueueAvail;
        let virtq_avail = unsafe { virtq_avail.as_mut().unwrap() };
        virtq_avail.init(VirtQueueAvailFlag::Default);

        let virtq_used = &mut ram[(virtq_used_base - ram_config::BASE_ADDR) as usize] as *mut u8
            as *mut VirtQueueUsed;
        let virtq_used = unsafe { virtq_used.as_mut().unwrap() };
        virtq_used.init(VirtQueueUsedFlag::Default);

        (virtq_desc_base, virtq_avail_base, virtq_used_base)
    }

    fn desc_table(ram: &mut Ram) -> *mut VirtQueueDesc {
        &mut ram[(DESC_TABLE_PADDR - ram_config::BASE_ADDR) as usize] as *mut u8
            as *mut VirtQueueDesc
    }

    fn guest_buffer(ram: &mut Ram, paddr: u64, len: usize) -> &mut [u8] {
        unsafe {
            slice::from_raw_parts_mut(&mut ram[(paddr - ram_config::BASE_ADDR) as usize], len)
        }
    }

    fn set_avail_entry(ram: &mut Ram, avail_base: u64, ring_idx: usize, desc_idx: u16) {
        let virtq_avail = &mut ram[(avail_base - ram_config::BASE_ADDR) as usize] as *mut u8
            as *mut VirtQueueAvail;
        let virtq_avail = unsafe { virtq_avail.as_mut().unwrap() };
        let ring = VirtQueueAvail::mut_ring(virtq_avail as *mut _ as u64, QUEUE_NUM as u32);
        ring[ring_idx] = desc_idx;
        virtq_avail.idx_atomic_add(1);
    }

    #[test]
    fn test_rng_device_basic() {
        let mut ram = Ram::new();
        let ram_base = &mut ram[0] as *mut u8;
        let mut device = VirtIORngDevice::new("VirtIO RNG 0", ram_base);

        assert_eq!(device.get_device_id(), VIRTIO_DEVICE_ID_RNG);
        assert_eq!(device.get_num_of_queue(), 1);

        let (_desc_base, avail_base, _used_base) = setup_queue(&mut ram, &mut device);
        assert!(device.queue_ready());

        // One device-writable buffer of 64 bytes.
        let buf_addr = 0x8000_2300u64;
        let buf_len = 0x40u32;
        unsafe {
            (*desc_table(&mut ram)).init(
                buf_addr,
                buf_len,
                VirtQueueDescFlag::VIRTQ_DESC_F_WRITE,
                0,
            );
        }
        // Poison the buffer before the request.
        guest_buffer(&mut ram, buf_addr, buf_len as usize).fill(0);

        set_avail_entry(&mut ram, avail_base, 0, 0);
        assert!(device.manage_one_request());

        // Data was filled with random (non-zero here) bytes.
        assert!(
            guest_buffer(&mut ram, buf_addr, buf_len as usize)
                .iter()
                .any(|&b| b != 0),
            "RNG buffer must not stay zero"
        );

        // Used ring reports the written length.
        let used_ring = device.queue.get_used_ring();
        assert_eq!(used_ring.get_index(), 1);
        let elem = used_ring.ring(QUEUE_NUM as u32)[0];
        assert_eq!(elem.get_len(), buf_len);
        assert_eq!(elem.get_id(), 0);

        // Interrupt is asserted and can be observed by the PLIC.
        assert_eq!(device.isr().load(Ordering::Acquire), 1);
        assert!(device.irq_level());
    }

    #[test]
    fn test_rng_device_returns_different_bytes() {
        let mut ram = Ram::new();
        let ram_base = &mut ram[0] as *mut u8;
        let mut device = VirtIORngDevice::new("VirtIO RNG 0", ram_base);

        let (_desc_base, avail_base, _used_base) = setup_queue(&mut ram, &mut device);
        let buf_addr = 0x8000_2300u64;
        unsafe {
            (*desc_table(&mut ram)).init(buf_addr, 0x20, VirtQueueDescFlag::VIRTQ_DESC_F_WRITE, 0);
        }

        set_avail_entry(&mut ram, avail_base, 0, 0);
        assert!(device.manage_one_request());
        let first = guest_buffer(&mut ram, buf_addr, 0x20).to_vec();

        set_avail_entry(&mut ram, avail_base, 1, 0);
        assert!(device.manage_one_request());
        let second = guest_buffer(&mut ram, buf_addr, 0x20).to_vec();

        assert_ne!(
            first, second,
            "two RNG requests must return different bytes"
        );
        assert_eq!(device.queue.get_used_ring().get_index(), 2);
    }

    #[test]
    fn test_rng_device_skips_readonly_buffers() {
        let mut ram = Ram::new();
        let ram_base = &mut ram[0] as *mut u8;
        let mut device = VirtIORngDevice::new("VirtIO RNG 0", ram_base);

        let (_desc_base, avail_base, _used_base) = setup_queue(&mut ram, &mut device);

        // Chain: read-only buffer (must not be touched) then writable buffer.
        let ro_addr = 0x8000_2300u64;
        let rw_addr = 0x8000_2400u64;
        unsafe {
            (*desc_table(&mut ram)).init(ro_addr, 0x20, VirtQueueDescFlag::VIRTQ_DESC_F_NEXT, 1);
            (*desc_table(&mut ram).add(1)).init(
                rw_addr,
                0x20,
                VirtQueueDescFlag::VIRTQ_DESC_F_WRITE,
                0,
            );
        }

        guest_buffer(&mut ram, ro_addr, 0x20).fill(0xAA);
        guest_buffer(&mut ram, rw_addr, 0x20).fill(0);

        set_avail_entry(&mut ram, avail_base, 0, 0);
        assert!(device.manage_one_request());

        assert!(
            guest_buffer(&mut ram, ro_addr, 0x20)
                .iter()
                .all(|&b| b == 0xAA),
            "read-only buffer must not be modified"
        );
        assert!(
            guest_buffer(&mut ram, rw_addr, 0x20)
                .iter()
                .any(|&b| b != 0)
        );

        // Only the writable buffer counts as written length.
        let used_ring = device.queue.get_used_ring();
        let elem = used_ring.ring(QUEUE_NUM as u32)[0];
        assert_eq!(elem.get_len(), 0x20);
    }

    #[test]
    fn test_rng_device_reset_and_features() {
        let mut ram = Ram::new();
        let ram_base = &mut ram[0] as *mut u8;
        let mut device = VirtIORngDevice::new("VirtIO RNG 0", ram_base);

        // No config space: reads return 0, writes are ignored.
        assert_eq!(device.read_config(0x0, 4), 0);

        // Unknown features are rejected and clear FEATURES_OK.
        device.set_feature(virtio_reserved_feature::VERSION_1);
        assert_eq!(device.guest_feature, virtio_reserved_feature::VERSION_1);
        device.set_feature(1u128 << 127);
        assert_eq!(device.status & VirtIODeviceStatus::FEATURES_OK.bits(), 0);

        device.isr.store(1, Ordering::Release);
        device.reset();
        assert_eq!(device.status, 0);
        assert_eq!(device.isr().load(Ordering::Acquire), 0);
        assert!(!device.irq_level());
    }
}
