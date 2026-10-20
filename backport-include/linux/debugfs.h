#ifndef __BACKPORT_LINUX_DEBUGFS_H
#define __BACKPORT_LINUX_DEBUGFS_H
#include <linux/version.h>
#include_next <linux/debugfs.h>

/*
 * Task#202：DEFINE_DEBUGFS_ATTRIBUTE_SIGNED() 是 6.0 引入的接口
 * （写入时允许负值）。5.10 只有无符号版本，这里退回普通版本；
 * 受影响的仅是 amdgpu 调试用的 ring error 注入接口。
 */
#ifndef DEFINE_DEBUGFS_ATTRIBUTE_SIGNED
#define DEFINE_DEBUGFS_ATTRIBUTE_SIGNED(__fops, __get, __set, __fmt)	\
	DEFINE_DEBUGFS_ATTRIBUTE(__fops, __get, __set, __fmt)
#endif

#endif /* __BACKPORT_LINUX_DEBUGFS_H */
