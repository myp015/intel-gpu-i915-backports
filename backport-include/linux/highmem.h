#ifndef __BACKPORT_LINUX_HIGHMEM_H
#define __BACKPORT_LINUX_HIGHMEM_H
#include <linux/version.h>
#include_next <linux/highmem.h>

/*
 * Task#202：amdgpu（v6.5）使用 kmap_local_page()/kunmap_local()，
 * 这两个接口 5.11 才引入，DSM 5.10.55 内核没有。
 * kmap()/kunmap() 在 5.10 中是内联实现，映射语义对驱动侧等价，
 * 因此用它们做兼容，不引入新的符号依赖。
 */
#ifdef BPM_KMAP_LOCAL_NOT_PRESENT
#ifndef kmap_local_page
#define kmap_local_page(page)			kmap(page)
#endif
#ifndef kunmap_local
#define kunmap_local(addr)			kunmap(kmap_to_page(addr))
#endif
#endif

#endif /* __BACKPORT_LINUX_HIGHMEM_H */
