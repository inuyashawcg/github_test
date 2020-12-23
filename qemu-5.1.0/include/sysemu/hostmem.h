/*
 * QEMU Host Memory Backend
 *
 * Copyright (C) 2013-2014 Red Hat Inc
 *
 * Authors:
 *   Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef SYSEMU_HOSTMEM_H
#define SYSEMU_HOSTMEM_H

#include "sysemu/numa.h"
#include "qapi/qapi-types-machine.h"
#include "qom/object.h"
#include "exec/memory.h"
#include "qemu/bitmap.h"

#define TYPE_MEMORY_BACKEND "memory-backend"
#define MEMORY_BACKEND(obj) \
    OBJECT_CHECK(HostMemoryBackend, (obj), TYPE_MEMORY_BACKEND)
#define MEMORY_BACKEND_GET_CLASS(obj) \
    OBJECT_GET_CLASS(HostMemoryBackendClass, (obj), TYPE_MEMORY_BACKEND)
#define MEMORY_BACKEND_CLASS(klass) \
    OBJECT_CLASS_CHECK(HostMemoryBackendClass, (klass), TYPE_MEMORY_BACKEND)

/* hostmem-ram.c */
/**
 * @TYPE_MEMORY_BACKEND_RAM:
 * name of backend that uses mmap on the anonymous RAM
 */

#define TYPE_MEMORY_BACKEND_RAM "memory-backend-ram"

/* hostmem-file.c */
/**
 * @TYPE_MEMORY_BACKEND_FILE:
 * name of backend that uses mmap on a file descriptor
 */
#define TYPE_MEMORY_BACKEND_FILE "memory-backend-file"

typedef struct HostMemoryBackend HostMemoryBackend;
typedef struct HostMemoryBackendClass HostMemoryBackendClass;

/**
 * HostMemoryBackendClass:
 * @parent_class: opaque parent class container
 */
struct HostMemoryBackendClass {
    ObjectClass parent_class;

    /* 分配内存使用？？ */
    void (*alloc)(HostMemoryBackend *backend, Error **errp);
};

/**
 * @HostMemoryBackend  backend：后端
 *
 * @parent: opaque parent object container
 * @size: amount of memory backend provides
 * @mr: MemoryRegion representing host memory belonging to backend
 *      MemoryRegion表示属于后端的主机内存
 * 
 * @prealloc_threads: number of threads to be used for preallocatining RAM
 *                    用于预分配RAM的线程数
 * 
 * HostMemoryBackend对象包含了客户机内存对应的真正的主机内存，这些内存可以是匿名映射的内存，
 * 也可以是文件映射内存。文件映射的客户机内存允许Linux在物理主机上透明大页机制的使用（hugetlbfs），
 * 并且能够共享内存，从而使其他进程可以访问客户机内存
 */
struct HostMemoryBackend {
    /* private */
    Object parent;

    /* protected */
    uint64_t size;
    bool merge, dump, use_canonical_path;   /* canonical: 典型的，经典的 */
    bool prealloc, is_mapped, share;
    uint32_t prealloc_threads;
    DECLARE_BITMAP(host_nodes, MAX_NODES + 1);
    HostMemPolicy policy;

    MemoryRegion mr;
};

bool host_memory_backend_mr_inited(HostMemoryBackend *backend);
MemoryRegion *host_memory_backend_get_memory(HostMemoryBackend *backend);

void host_memory_backend_set_mapped(HostMemoryBackend *backend, bool mapped);
bool host_memory_backend_is_mapped(HostMemoryBackend *backend);
size_t host_memory_backend_pagesize(HostMemoryBackend *memdev);
char *host_memory_backend_get_name(HostMemoryBackend *backend);

#endif
