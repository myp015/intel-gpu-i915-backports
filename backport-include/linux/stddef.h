#ifndef __BACKPORT_LINUX_STDDEF_H
#define __BACKPORT_LINUX_STDDEF_H
#include <linux/version.h>
#include_next <linux/stddef.h>

/*
 * Task#202：DECLARE_FLEX_ARRAY() 是 5.17 引入的接口，amdgpu 的
 * discovery 表（struct ip_v4）用它在一个 union 里声明可变长数组。
 * 这里是内核头中的等价实现。
 */
#ifndef DECLARE_FLEX_ARRAY
#define DECLARE_FLEX_ARRAY(TYPE, NAME)	\
	struct {			\
		struct { } __empty_ ## NAME;	\
		TYPE NAME[];		\
	}
#endif

#endif /* __BACKPORT_LINUX_STDDEF_H */
