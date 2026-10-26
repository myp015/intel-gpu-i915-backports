/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 *          Christian König
 */

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <linux/kernel.h>

#include "radeon.h"

/*
 * Rings
 * Most engines on the GPU are fed via ring buffers.  Ring
 * buffers are areas of GPU accessible memory that the host
 * writes commands into and the GPU reads commands out of.
 * There is a rptr (read pointer) that determines where the
 * GPU is currently reading, and a wptr (write pointer)
 * which determines where the host has written.  When the
 * pointers are equal, the ring is idle.  When the host
 * writes commands to the ring buffer, it increments the
 * wptr.  The GPU then starts fetching commands and executes
 * them until the pointers are equal again.
 */
static void radeon_debugfs_ring_init(struct radeon_device *rdev, struct radeon_ring *ring);

/**
 * radeon_ring_supports_scratch_reg - check if the ring supports
 * writing to scratch registers
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Check if a specific ring supports writing to scratch registers (all asics).
 * Returns true if the ring supports writing to scratch regs, false if not.
 */
bool radeon_ring_supports_scratch_reg(struct radeon_device *rdev,
				      struct radeon_ring *ring)
{
	switch (ring->idx) {
	case RADEON_RING_TYPE_GFX_INDEX:
	case CAYMAN_RING_TYPE_CP1_INDEX:
	case CAYMAN_RING_TYPE_CP2_INDEX:
		return true;
	default:
		return false;
	}
}

/**
 * radeon_ring_free_size - update the free size
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Update the free dw slots in the ring buffer (all asics).
 */
void radeon_ring_free_size(struct radeon_device *rdev, struct radeon_ring *ring)
{
	uint32_t rptr = radeon_ring_get_rptr(rdev, ring);

	/* This works because ring_size is a power of 2 */
	ring->ring_free_dw = rptr + (ring->ring_size / 4);
	ring->ring_free_dw -= ring->wptr;
	ring->ring_free_dw &= ring->ptr_mask;
	if (!ring->ring_free_dw) {
		/* this is an empty ring */
		ring->ring_free_dw = ring->ring_size / 4;
		/*  update lockup info to avoid false positive */
		radeon_ring_lockup_update(rdev, ring);
	}
}

/**
 * radeon_ring_alloc - allocate space on the ring buffer
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 * @ndw: number of dwords to allocate in the ring buffer
 *
 * Allocate @ndw dwords in the ring buffer (all asics).
 * Returns 0 on success, error on failure.
 */
int radeon_ring_alloc(struct radeon_device *rdev, struct radeon_ring *ring, unsigned ndw)
{
	int r;

	/* make sure we aren't trying to allocate more space than there is on the ring */
	if (ndw > (ring->ring_size / 4))
		return -ENOMEM;
	/* Align requested size with padding so unlock_commit can
	 * pad safely */
	radeon_ring_free_size(rdev, ring);
	ndw = (ndw + ring->align_mask) & ~ring->align_mask;
	while (ndw > (ring->ring_free_dw - 1)) {
		radeon_ring_free_size(rdev, ring);
		if (ndw < ring->ring_free_dw) {
			break;
		}
		r = radeon_fence_wait_next(rdev, ring->idx);
		if (r)
			return r;
	}
	ring->count_dw = ndw;
	ring->wptr_old = ring->wptr;
	return 0;
}

/**
 * radeon_ring_lock - lock the ring and allocate space on it
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 * @ndw: number of dwords to allocate in the ring buffer
 *
 * Lock the ring and allocate @ndw dwords in the ring buffer
 * (all asics).
 * Returns 0 on success, error on failure.
 */
int radeon_ring_lock(struct radeon_device *rdev, struct radeon_ring *ring, unsigned ndw)
{
	int r;

	mutex_lock(&rdev->ring_lock);
	r = radeon_ring_alloc(rdev, ring, ndw);
	if (r) {
		mutex_unlock(&rdev->ring_lock);
		return r;
	}
	return 0;
}

/**
 * radeon_ring_commit - tell the GPU to execute the new
 * commands on the ring buffer
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 * @hdp_flush: Whether or not to perform an HDP cache flush
 *
 * Update the wptr (write pointer) to tell the GPU to
 * execute new commands on the ring buffer (all asics).
 */
void radeon_ring_commit(struct radeon_device *rdev, struct radeon_ring *ring,
			bool hdp_flush)
{
	/* If we are emitting the HDP flush via the ring buffer, we need to
	 * do it before padding.
	 */
	if (hdp_flush && rdev->asic->ring[ring->idx]->hdp_flush)
		rdev->asic->ring[ring->idx]->hdp_flush(rdev, ring);
	/* We pad to match fetch size */
	while (ring->wptr & ring->align_mask) {
		radeon_ring_write(ring, ring->nop);
	}
	mb();
	/* If we are emitting the HDP flush via MMIO, we need to do it after
	 * all CPU writes to VRAM finished.
	 */
	if (hdp_flush && rdev->asic->mmio_hdp_flush)
		rdev->asic->mmio_hdp_flush(rdev);
	radeon_ring_set_wptr(rdev, ring);
}

/**
 * radeon_ring_unlock_commit - tell the GPU to execute the new
 * commands on the ring buffer and unlock it
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 * @hdp_flush: Whether or not to perform an HDP cache flush
 *
 * Call radeon_ring_commit() then unlock the ring (all asics).
 */
void radeon_ring_unlock_commit(struct radeon_device *rdev, struct radeon_ring *ring,
			       bool hdp_flush)
{
	radeon_ring_commit(rdev, ring, hdp_flush);
	mutex_unlock(&rdev->ring_lock);
}

/**
 * radeon_ring_undo - reset the wptr
 *
 * @ring: radeon_ring structure holding ring information
 *
 * Reset the driver's copy of the wptr (all asics).
 */
void radeon_ring_undo(struct radeon_ring *ring)
{
	ring->wptr = ring->wptr_old;
}

/**
 * radeon_ring_unlock_undo - reset the wptr and unlock the ring
 *
 * @rdev:       radeon device structure
 * @ring: radeon_ring structure holding ring information
 *
 * Call radeon_ring_undo() then unlock the ring (all asics).
 */
void radeon_ring_unlock_undo(struct radeon_device *rdev, struct radeon_ring *ring)
{
	radeon_ring_undo(ring);
	mutex_unlock(&rdev->ring_lock);
}

/**
 * radeon_ring_lockup_update - update lockup variables
 *
 * @rdev:       radeon device structure
 * @ring: radeon_ring structure holding ring information
 *
 * Update the last rptr value and timestamp (all asics).
 */
void radeon_ring_lockup_update(struct radeon_device *rdev,
			       struct radeon_ring *ring)
{
	atomic_set(&ring->last_rptr, radeon_ring_get_rptr(rdev, ring));
	atomic64_set(&ring->last_activity, jiffies_64);
}

/**
 * radeon_ring_test_lockup() - check if ring is lockedup by recording information
 * @rdev:       radeon device structure
 * @ring:       radeon_ring structure holding ring information
 *
 */
bool radeon_ring_test_lockup(struct radeon_device *rdev, struct radeon_ring *ring)
{
	uint32_t rptr = radeon_ring_get_rptr(rdev, ring);
	uint64_t last = atomic64_read(&ring->last_activity);
	uint64_t elapsed;

	if (rptr != atomic_read(&ring->last_rptr)) {
		/* ring is still working, no lockup */
		radeon_ring_lockup_update(rdev, ring);
		return false;
	}

	elapsed = jiffies_to_msecs(jiffies_64 - last);
	if (radeon_lockup_timeout && elapsed >= radeon_lockup_timeout) {
		dev_err(rdev->dev, "ring %d stalled for more than %llumsec\n",
			ring->idx, elapsed);
		return true;
	}
	/* give a chance to the GPU ... */
	return false;
}

/*
 * Task#209：GPU 挂死取证。
 *
 * 现场只有 "ring N stalled" 时无法判断引擎停在哪条命令上。这里把环内
 * 尚未被消费的命令按 dword 转储，并对 DMA 环（TRINITY/SI 的 TTM 搬运
 * 固定走 ring 3/4）的包做最小解码：卡住的 COPY 包直接给出源/目的 GPU
 * 地址与长度，可与复位时 VM_CONTEXT*_PROTECTION_FAULT_ADDR 对照。
 * 只在首次确认挂死、以及 GPU 复位备份环时调用，属一次性现场记录。
 */
#define RADEON_FORENSICS_MAX_DW 64

static const char *radeon_forensics_dma_op(u32 op)
{
	switch (op) {
	case 0x2: return "WRITE";
	case 0x3: return "COPY";
	case 0x4: return "INDIRECT_BUFFER";
	case 0x5: return "SEMAPHORE";
	case 0x6: return "FENCE";
	case 0x7: return "TRAP";
	case 0x9: return "SRBM_WRITE";
	case 0xd: return "CONSTANT_FILL";
	case 0xe: return "POLL_REG_MEM";
	case 0xf: return "NOP";
	default:  return "UNKNOWN";
	}
}

/* DMA 包总长（dword）。未知类型只前进 1 dword；长度一律裁剪到剩余量。 */
static unsigned radeon_forensics_dma_len(u32 hdr, unsigned avail)
{
	unsigned len;

	switch (hdr >> 28) {
	case 0x3: /* COPY：header + dst_lo/src_lo/dst_hi/src_hi */
		len = 5;
		break;
	case 0x4: /* INDIRECT_BUFFER：header + addr+len + addr_hi */
		len = 3;
		break;
	case 0x2: /* WRITE：header + dst 两 dword + count 个数据 dword */
		len = 3 + (hdr & 0xfffff);
		break;
	case 0x9: /* SRBM_WRITE：header + reg + value */
		len = 3;
		break;
	case 0xe: /* POLL_REG_MEM：header + 5 dword */
		len = 6;
		break;
	default:
		len = 1;
		break;
	}
	return len < avail ? len : avail;
}

static void radeon_forensics_dump_dma(struct radeon_device *rdev,
				      const u32 *data, unsigned ndw)
{
	unsigned i = 0;

	while (i < ndw) {
		u32 hdr = data[i];
		u32 op = hdr >> 28;
		unsigned len = radeon_forensics_dma_len(hdr, ndw - i);

		if (op == 0x3 && len == 5) {
			u64 dst = (u64)data[i + 1] |
				  ((u64)(data[i + 3] & 0xff) << 32);
			u64 src = (u64)data[i + 2] |
				  ((u64)(data[i + 4] & 0xff) << 32);

			/* 第 26 位：SI 的 5 参数宏用它表示按字节计数 */
			dev_err(rdev->dev,
				 "  [%3u] DMA COPY dst=0x%010llx src=0x%010llx count=%u %s\n",
				 i, dst, src, hdr & 0xfffff,
				 (hdr & (1 << 26)) ? "bytes" : "dwords");
		} else if (op == 0x4 && len == 3) {
			u64 addr = (u64)(data[i + 1] & 0xffffffe0) |
				   ((u64)(data[i + 2] & 0xff) << 32);

			dev_err(rdev->dev,
				 "  [%3u] DMA IB addr=0x%010llx len=%u dw vmid=%u\n",
				 i, addr, (data[i + 2] >> 12) & 0xfffff,
				 (hdr >> 20) & 0xf);
		} else if (op == 0x2 && len >= 3) {
			u64 dst = (u64)data[i + 1] |
				  ((u64)(data[i + 2] & 0xff) << 32);

			dev_err(rdev->dev,
				 "  [%3u] DMA WRITE dst=0x%010llx count=%u dwords\n",
				 i, dst, hdr & 0xfffff);
		} else if (op == 0x9 && len == 3) {
			dev_err(rdev->dev,
				 "  [%3u] DMA SRBM_WRITE reg=0x%05x value=0x%08x\n",
				 i, (data[i + 1] >> 2) & 0xffff, data[i + 2]);
		} else {
			dev_err(rdev->dev, "  [%3u] DMA %s header=0x%08x\n",
				 i, radeon_forensics_dma_op(op), hdr);
		}
		i += len;
	}
}

static void radeon_forensics_dump_window(struct radeon_device *rdev,
					 const char *tag,
					 struct radeon_ring *ring,
					 const u32 *data, unsigned ndw,
					 u32 start)
{
	unsigned i;

	for (i = 0; i < ndw; i += 8) {
		char line[8 * 9 + 1];
		unsigned j, n = ndw - i < 8 ? ndw - i : 8;
		int off = 0;

		for (j = 0; j < n; j++)
			off += scnprintf(line + off, sizeof(line) - off,
					 " %08x", data[i + j]);
		dev_err(rdev->dev, "  %s[%04x]:%s\n", tag,
			 (start + i) & ring->ptr_mask, line);
	}
}

static bool radeon_ring_is_dma_ring(struct radeon_ring *ring)
{
	return ring->idx == R600_RING_TYPE_DMA_INDEX ||
	       ring->idx == CAYMAN_RING_TYPE_DMA1_INDEX;
}

/**
 * radeon_ring_forensics - dump a ring's unprocessed commands (lockup)
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Dump the commands between the hardware read pointer and the write
 * pointer (maximum RADEON_FORENSICS_MAX_DW dwords) and decode DMA
 * packets when the ring is one of the async DMA rings.
 */
void radeon_ring_forensics(struct radeon_device *rdev, struct radeon_ring *ring)
{
	const struct radeon_asic_ring *ar = rdev->asic->ring[ring->idx];
	struct radeon_fence_driver *drv = &rdev->fence_drv[ring->idx];
	u32 buf[RADEON_FORENSICS_MAX_DW];
	u32 wptr = ring->wptr, rptr = ring->wptr;
	uint64_t last;
	unsigned ndw, dump_dw, i;

	if (!ring->ready || !ring->ring || !ring->ring_obj)
		return;

	if (ar && ar->get_rptr)
		rptr = ar->get_rptr(rdev, ring);

	ndw = (wptr - rptr) & ring->ptr_mask;
	last = atomic64_read(&ring->last_activity);

	dev_err(rdev->dev,
		 "ring %u forensics: wptr=%u hw_rptr=%u pending=%u dw fence=0x%016llx completed=0x%016llx fence_addr=0x%016llx (%s)\n",
		 ring->idx, wptr, rptr, ndw, drv->sync_seq[ring->idx],
		 (u64)atomic64_read(&drv->last_seq), drv->gpu_addr,
		 drv->cpu_addr ? "wb" : "scratch");
	if (last)
		dev_err(rdev->dev, "  last ring progress %llums ago\n",
			 (unsigned long long)jiffies_to_msecs(jiffies_64 - last));

	if (!ndw) {
		dev_err(rdev->dev, "  ring is empty (wptr == hw_rptr)\n");
		return;
	}

	dump_dw = ndw < RADEON_FORENSICS_MAX_DW ? ndw : RADEON_FORENSICS_MAX_DW;
	if (dump_dw != ndw)
		dev_err(rdev->dev,
			 "  showing first %u of %u pending dwords\n",
			 dump_dw, ndw);

	for (i = 0; i < dump_dw; i++)
		buf[i] = ring->ring[(rptr + i) & ring->ptr_mask];

	radeon_forensics_dump_window(rdev, "ring", ring, buf, dump_dw, rptr);
	if (radeon_ring_is_dma_ring(ring))
		radeon_forensics_dump_dma(rdev, buf, dump_dw);
}

/**
 * radeon_ring_forensics_saved - dump commands saved by radeon_ring_backup
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 * @data: saved dwords
 * @ndw: number of saved dwords
 *
 * GPU 复位会重新初始化引擎，备份下来的命令就是挂死时尚未被消费的那批，
 * 直接按备份内容解码，避免复位后再读环造成的误导。
 */
void radeon_ring_forensics_saved(struct radeon_device *rdev,
				 struct radeon_ring *ring,
				 const u32 *data, unsigned ndw)
{
	unsigned dump_dw = ndw < RADEON_FORENSICS_MAX_DW ?
			   ndw : RADEON_FORENSICS_MAX_DW;

	if (!data || !ndw)
		return;

	dev_err(rdev->dev, "ring %u: %u dwords pending at reset\n",
		 ring->idx, ndw);
	radeon_forensics_dump_window(rdev, "saved", ring, data, dump_dw, 0);
	if (radeon_ring_is_dma_ring(ring))
		radeon_forensics_dump_dma(rdev, data, dump_dw);
}

/**
 * radeon_ring_backup - Back up the content of a ring
 *
 * @rdev: radeon_device pointer
 * @ring: the ring we want to back up
 * @data: placeholder for returned commit data
 *
 * Saves all unprocessed commits from a ring, returns the number of dwords saved.
 */
unsigned radeon_ring_backup(struct radeon_device *rdev, struct radeon_ring *ring,
			    uint32_t **data)
{
	unsigned size, ptr, i;

	/* just in case lock the ring */
	mutex_lock(&rdev->ring_lock);
	*data = NULL;

	if (ring->ring_obj == NULL) {
		mutex_unlock(&rdev->ring_lock);
		return 0;
	}

	/* it doesn't make sense to save anything if all fences are signaled */
	if (!radeon_fence_count_emitted(rdev, ring->idx)) {
		mutex_unlock(&rdev->ring_lock);
		return 0;
	}

	/* calculate the number of dw on the ring */
	if (ring->rptr_save_reg)
		ptr = RREG32(ring->rptr_save_reg);
	else if (rdev->wb.enabled)
		ptr = le32_to_cpu(*ring->next_rptr_cpu_addr);
	else {
		/* no way to read back the next rptr */
		mutex_unlock(&rdev->ring_lock);
		return 0;
	}

	size = ring->wptr + (ring->ring_size / 4);
	size -= ptr;
	size &= ring->ptr_mask;
	if (size == 0) {
		mutex_unlock(&rdev->ring_lock);
		return 0;
	}

	/* and then save the content of the ring */
	*data = kvmalloc_array(size, sizeof(uint32_t), GFP_KERNEL);
	if (!*data) {
		mutex_unlock(&rdev->ring_lock);
		return 0;
	}
	for (i = 0; i < size; ++i) {
		(*data)[i] = ring->ring[ptr++];
		ptr &= ring->ptr_mask;
	}

	mutex_unlock(&rdev->ring_lock);
	return size;
}

/**
 * radeon_ring_restore - append saved commands to the ring again
 *
 * @rdev: radeon_device pointer
 * @ring: ring to append commands to
 * @size: number of dwords we want to write
 * @data: saved commands
 *
 * Allocates space on the ring and restore the previously saved commands.
 */
int radeon_ring_restore(struct radeon_device *rdev, struct radeon_ring *ring,
			unsigned size, uint32_t *data)
{
	int i, r;

	if (!size || !data)
		return 0;

	/* restore the saved ring content */
	r = radeon_ring_lock(rdev, ring, size);
	if (r)
		return r;

	for (i = 0; i < size; ++i) {
		radeon_ring_write(ring, data[i]);
	}

	radeon_ring_unlock_commit(rdev, ring, false);
	kvfree(data);
	return 0;
}

/**
 * radeon_ring_init - init driver ring struct.
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 * @ring_size: size of the ring
 * @rptr_offs: offset of the rptr writeback location in the WB buffer
 * @nop: nop packet for this ring
 *
 * Initialize the driver information for the selected ring (all asics).
 * Returns 0 on success, error on failure.
 */
int radeon_ring_init(struct radeon_device *rdev, struct radeon_ring *ring, unsigned ring_size,
		     unsigned rptr_offs, u32 nop)
{
	int r;

	ring->ring_size = ring_size;
	ring->rptr_offs = rptr_offs;
	ring->nop = nop;
	ring->rdev = rdev;
	/* Allocate ring buffer */
	if (ring->ring_obj == NULL) {
		r = radeon_bo_create(rdev, ring->ring_size, PAGE_SIZE, true,
				     RADEON_GEM_DOMAIN_GTT, 0, NULL,
				     NULL, &ring->ring_obj);
		if (r) {
			dev_err(rdev->dev, "(%d) ring create failed\n", r);
			return r;
		}
		r = radeon_bo_reserve(ring->ring_obj, false);
		if (unlikely(r != 0))
			return r;
		r = radeon_bo_pin(ring->ring_obj, RADEON_GEM_DOMAIN_GTT,
					&ring->gpu_addr);
		if (r) {
			radeon_bo_unreserve(ring->ring_obj);
			dev_err(rdev->dev, "(%d) ring pin failed\n", r);
			return r;
		}
		r = radeon_bo_kmap(ring->ring_obj,
				       (void **)&ring->ring);
		radeon_bo_unreserve(ring->ring_obj);
		if (r) {
			dev_err(rdev->dev, "(%d) ring map failed\n", r);
			return r;
		}
	}
	ring->ptr_mask = (ring->ring_size / 4) - 1;
	ring->ring_free_dw = ring->ring_size / 4;
	if (rdev->wb.enabled) {
		u32 index = RADEON_WB_RING0_NEXT_RPTR + (ring->idx * 4);
		ring->next_rptr_gpu_addr = rdev->wb.gpu_addr + index;
		ring->next_rptr_cpu_addr = &rdev->wb.wb[index/4];
	}
	radeon_debugfs_ring_init(rdev, ring);
	radeon_ring_lockup_update(rdev, ring);
	return 0;
}

/**
 * radeon_ring_fini - tear down the driver ring struct.
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Tear down the driver information for the selected ring (all asics).
 */
void radeon_ring_fini(struct radeon_device *rdev, struct radeon_ring *ring)
{
	int r;
	struct radeon_bo *ring_obj;

	mutex_lock(&rdev->ring_lock);
	ring_obj = ring->ring_obj;
	ring->ready = false;
	ring->ring = NULL;
	ring->ring_obj = NULL;
	mutex_unlock(&rdev->ring_lock);

	if (ring_obj) {
		r = radeon_bo_reserve(ring_obj, false);
		if (likely(r == 0)) {
			radeon_bo_kunmap(ring_obj);
			radeon_bo_unpin(ring_obj);
			radeon_bo_unreserve(ring_obj);
		}
		radeon_bo_unref(&ring_obj);
	}
}

/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)

static int radeon_debugfs_ring_info_show(struct seq_file *m, void *unused)
{
	struct radeon_ring *ring = m->private;
	struct radeon_device *rdev = ring->rdev;

	uint32_t rptr, wptr, rptr_next;
	unsigned count, i, j;

	radeon_ring_free_size(rdev, ring);
	count = (ring->ring_size / 4) - ring->ring_free_dw;

	wptr = radeon_ring_get_wptr(rdev, ring);
	seq_printf(m, "wptr: 0x%08x [%5d]\n",
		   wptr, wptr);

	rptr = radeon_ring_get_rptr(rdev, ring);
	seq_printf(m, "rptr: 0x%08x [%5d]\n",
		   rptr, rptr);

	if (ring->rptr_save_reg) {
		rptr_next = RREG32(ring->rptr_save_reg);
		seq_printf(m, "rptr next(0x%04x): 0x%08x [%5d]\n",
			   ring->rptr_save_reg, rptr_next, rptr_next);
	} else
		rptr_next = ~0;

	seq_printf(m, "driver's copy of the wptr: 0x%08x [%5d]\n",
		   ring->wptr, ring->wptr);
	seq_printf(m, "last semaphore signal addr : 0x%016llx\n",
		   ring->last_semaphore_signal_addr);
	seq_printf(m, "last semaphore wait addr   : 0x%016llx\n",
		   ring->last_semaphore_wait_addr);
	seq_printf(m, "%u free dwords in ring\n", ring->ring_free_dw);
	seq_printf(m, "%u dwords in ring\n", count);

	if (!ring->ring)
		return 0;

	/* print 8 dw before current rptr as often it's the last executed
	 * packet that is the root issue
	 */
	i = (rptr + ring->ptr_mask + 1 - 32) & ring->ptr_mask;
	for (j = 0; j <= (count + 32); j++) {
		seq_printf(m, "r[%5d]=0x%08x", i, ring->ring[i]);
		if (rptr == i)
			seq_puts(m, " *");
		if (rptr_next == i)
			seq_puts(m, " #");
		seq_puts(m, "\n");
		i = (i + 1) & ring->ptr_mask;
	}
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(radeon_debugfs_ring_info);

static const char *radeon_debugfs_ring_idx_to_name(uint32_t ridx)
{
	switch (ridx) {
	case RADEON_RING_TYPE_GFX_INDEX:
		return "radeon_ring_gfx";
	case CAYMAN_RING_TYPE_CP1_INDEX:
		return "radeon_ring_cp1";
	case CAYMAN_RING_TYPE_CP2_INDEX:
		return "radeon_ring_cp2";
	case R600_RING_TYPE_DMA_INDEX:
		return "radeon_ring_dma1";
	case CAYMAN_RING_TYPE_DMA1_INDEX:
		return "radeon_ring_dma2";
	case R600_RING_TYPE_UVD_INDEX:
		return "radeon_ring_uvd";
	case TN_RING_TYPE_VCE1_INDEX:
		return "radeon_ring_vce1";
	case TN_RING_TYPE_VCE2_INDEX:
		return "radeon_ring_vce2";
	default:
		return NULL;

	}
}
#endif

static void radeon_debugfs_ring_init(struct radeon_device *rdev, struct radeon_ring *ring)
{
#if defined(CONFIG_DEBUG_FS)
	const char *ring_name = radeon_debugfs_ring_idx_to_name(ring->idx);
	struct dentry *root = rdev->ddev->primary->debugfs_root;

	if (ring_name)
		debugfs_create_file(ring_name, 0444, root, ring,
				    &radeon_debugfs_ring_info_fops);

#endif
}
