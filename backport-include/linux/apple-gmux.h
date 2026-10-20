#ifndef __BACKPORT_LINUX_APPLE_GMUX_H
#define __BACKPORT_LINUX_APPLE_GMUX_H
#include <linux/version.h>
#include_next <linux/apple-gmux.h>

/*
 * Task#202：apple_gmux_detect() 是 6.1 引入的接口，旧内核只有
 * apple_gmux_present()。amdgpu 调用时两个输出参数都传 NULL，
 * 语义与 apple_gmux_present() 等价。
 */
#if LINUX_VERSION_IS_LESS(6,1,0)
#ifndef apple_gmux_detect
#define apple_gmux_detect(mux, no_mux)	apple_gmux_present()
#endif
#endif

#endif /* __BACKPORT_LINUX_APPLE_GMUX_H */
