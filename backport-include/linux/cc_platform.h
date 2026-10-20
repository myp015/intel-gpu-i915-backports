#ifndef __BACKPORT_LINUX_CC_PLATFORM_H
#define __BACKPORT_LINUX_CC_PLATFORM_H
#include <linux/version.h>
#include <linux/types.h>

#if LINUX_VERSION_IS_LESS(5,15,0)
/*
 * Task#202：cc_platform.h 5.15 才引入。DSM 5.10.55 内核没有
 * CONFIG_AMD_MEM_ENCRYPT，官方导出面也不含 mem_encrypt_active/sme_active，
 * 因此这里按"未启用内存加密"返回 false（amdgpu 只在 RAVEN 的
 * S/G display 兼容性判断中使用该属性）。
 */
#ifndef CC_ATTR_MEM_ENCRYPT
#define CC_ATTR_MEM_ENCRYPT 0
#endif

static inline bool cc_platform_has(u32 attr)
{
	return false;
}
#else
#include_next <linux/cc_platform.h>
#endif

#endif /* __BACKPORT_LINUX_CC_PLATFORM_H */
