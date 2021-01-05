/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright Rusty Russell IBM Corporation 2007.
 *
 * This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: releng/12.0/sys/dev/virtio/virtio_ring.h 335305 2018-06-17 20:45:48Z bryanv $
 */

#ifndef VIRTIO_RING_H
#define	VIRTIO_RING_H

/* This marks a buffer as continuing via the next field. 
    用于表明当前 buffer 的下一个域是否有效，也间接表明当前 buffer 是否是 buffers list 的最后一个
*/
#define VRING_DESC_F_NEXT       1

/* This marks a buffer as write-only (otherwise read-only). */
#define VRING_DESC_F_WRITE      2

/* This means the buffer contains a list of buffer descriptors. 
    表明这个 buffer 中包含一个 buffer 描述符的 list
*/
#define VRING_DESC_F_INDIRECT	4

/* The Host uses this in used->flags to advise the Guest: don't kick me
 * when you add a buffer.  It's unreliable, so it's simply an
 * optimization.  Guest will still kick if it's out of buffers. */
#define VRING_USED_F_NO_NOTIFY  1

/* The Guest uses this in avail->flags to advise the Host: don't
 * interrupt me when you consume a buffer.  It's unreliable, so it's
 * simply an optimization.
 * 来宾在avail->flags中使用这个命令来通知主机：在使用缓冲区时不要打断我。这是不可靠的，
 * 所以它只是一个优化
 */
#define VRING_AVAIL_F_NO_INTERRUPT      1

/* VirtIO ring descriptors: 16 bytes.
 * These can chain together via "next". 
 * 用于管理所有的ring，要注意仅仅起到管理作用，
 * 里边包含的仅仅是管理信息，而不是真正的 ring */
struct vring_desc {
        /* Address (guest-physical). guest物理地址 */
        uint64_t addr;
        /* Length. */
        uint32_t len;
        /* The flags as indicated above. */
        uint16_t flags;
        /* We chain unused descriptors via this, too. 
            - 通过该成员将未使用到的描述符串联起来
            - 所有的 buffers 通过 next 串联起来组成 descriptor table
            约定俗成，每个 list 中，read-only buffers 放置在 write-only buffers 前面
        */
        uint16_t next; 
};

struct vring_avail {
        uint16_t flags;
        uint16_t idx;
        uint16_t ring[0];
};

/* uint32_t is used here for ids for padding reasons. uint32_t在这里用于填充id */
struct vring_used_elem {
        /* Index of start of used descriptor chain. 已使用描述符列表的起始索引 */
        uint32_t id;
        /* Total length of the descriptor chain which was written to. 需要写入的描述符列表的总长度 */
        uint32_t len;
};

struct vring_used {
        uint16_t flags;
        uint16_t idx;
        struct vring_used_elem ring[0];
};

/* 可以看到其中包含有三个数组成员，分别对应描述符列表，已使用的描述符和可用的描述符 
    https://www.ibm.com/developerworks/cn/linux/1402_caobb_virtio/index.html
*/
struct vring {
        unsigned int num;
        /*
            描述符数组（descriptor table）用于存储一些关联的描述符，每个描述符都是一个对 buffer 的描述，
            包含一个 address/length 的配对
        */
        struct vring_desc *desc;
        /* 
            可用的 ring(available ring)用于 guest 端表示那些描述符链当前是可用的
            - Available ring 指向 guest 提供给设备的描述符,它指向一个 descriptor 链表的头,
            flags 值为 0 或者 1，1 表明 Guest 不需要 device 使用完这些 descriptor 时上报中断,
            idx 指向我们下一个 descriptor 入口处，idx 从 0 开始，一直增加，使用时需要取模
        */
        struct vring_avail *avail;
        /* 使用过的 ring(used ring)用于表示 Host 端表示那些描述符已经使用 
            Used ring 指向 device(host)使用过的 buffers。Used ring 和 Available ring 之间在内存中的
            分布会有一定间隙，从而避免了 host 和 guest 两端由于 cache 的影响而会写入到 virtqueue 结构体的
            同一部分的情况
        */
        struct vring_used *used;
};

/* Alignment requirements for vring elements.
 * When using pre-virtio 1.0 layout, these fall out naturally.
 */
#define VRING_AVAIL_ALIGN_SIZE 2
#define VRING_USED_ALIGN_SIZE 4
#define VRING_DESC_ALIGN_SIZE 16

/* The standard layout for the ring is a continuous chunk of memory which
 * looks like this.  We assume num is a power of 2.
 *
 * struct vring {
 *      // The actual descriptors (16 bytes each)
 *      struct vring_desc desc[num];
 *
 *      // A ring of available descriptor heads with free-running index.
 *      __u16 avail_flags;
 *      __u16 avail_idx;
 *      __u16 available[num];
 *      __u16 used_event_idx;
 *
 *      // Padding to the next align boundary.
 *      char pad[];
 *
 *      // A ring of used descriptor heads with free-running index.
 *      __u16 used_flags;
 *      __u16 used_idx;
 *      struct vring_used_elem used[num];
 *      __u16 avail_event_idx;
 * };
 *
 * NOTE: for VirtIO PCI, align is 4096.
 */

/*
 * We publish the used event index at the end of the available ring, and vice
 * versa. They are at the end for backwards compatibility.
 * 我们在可用环的末尾发布所使用的事件索引，反之亦然。它们位于向后兼容性的末尾
 */
#define vring_used_event(vr)	((vr)->avail->ring[(vr)->num])
#define vring_avail_event(vr)	(*(uint16_t *)&(vr)->used->ring[(vr)->num])

static inline int
vring_size(unsigned int num, unsigned long align)
{
	int size;

	size = num * sizeof(struct vring_desc);
	size += sizeof(struct vring_avail) + (num * sizeof(uint16_t)) +
	    sizeof(uint16_t);
	size = (size + align - 1) & ~(align - 1);
	size += sizeof(struct vring_used) +
	    (num * sizeof(struct vring_used_elem)) + sizeof(uint16_t);
	return (size);
}

/*
    从virtqueue.c - virtqueue_alloc 函数代码逻辑来看，virtqueue会分配一块连续的物理内存，
    起始地址赋值给 vq->vq_ring_mem(最终会传递给参数p)
*/
static inline void
vring_init(struct vring *vr, unsigned int num, uint8_t *p,
    unsigned long align)
{
        vr->num = num;
        vr->desc = (struct vring_desc *) p;
        vr->avail = (struct vring_avail *) (p +
	    num * sizeof(struct vring_desc));
        vr->used = (void *)
	    (((unsigned long) &vr->avail->ring[num] + align-1) & ~(align-1));
}

/*
 * The following is used with VIRTIO_RING_F_EVENT_IDX.
 *
 * Assuming a given event_idx value from the other size, if we have
 * just incremented index from old to new_idx, should we trigger an
 * event?
 * 假设一个给定的事件的idx值来自另一个大小，如果我们刚刚将索引从旧的增加到新的idx，
 * 我们应该触发一个事件吗？
 */
static inline int
vring_need_event(uint16_t event_idx, uint16_t new_idx, uint16_t old)
{

	return (uint16_t)(new_idx - event_idx - 1) < (uint16_t)(new_idx - old);
}
#endif /* VIRTIO_RING_H */
