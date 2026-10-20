#ifndef __BACKPORT_LINUX_WORKQUEUE_H
#define __BACKPORT_LINUX_WORKQUEUE_H
#include <linux/version.h>
#include_next <linux/workqueue.h>

/*
 * Task#202：cancel_work() 是 6.0 才恢复的内核接口。
 * 旧内核没有非阻塞的尝试取消接口，这里映射到 cancel_work_sync()
 * （amdgpu 只在 GPU recover 的进程上下文中使用）。
 */
#ifndef BPM_CANCEL_WORK_AVAILABLE
#ifndef cancel_work
#define cancel_work(work)		cancel_work_sync(work)
#endif
#endif

#endif /* __BACKPORT_LINUX_WORKQUEUE_H */
