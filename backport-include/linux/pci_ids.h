#ifndef __BACKPORT_LINUX_PCI_IDS_H
#define __BACKPORT_LINUX_PCI_IDS_H
#include <linux/version.h>
#include_next <linux/pci_ids.h>

/*
 * Task#202：PCI_CLASS_ACCELERATOR_PROCESSING 是 6.2 加入 pci_ids.h 的
 * 基础类码，5.10 没有。amdgpu 用它匹配 IP discovery 加速卡。
 */
#if LINUX_VERSION_IS_LESS(6,2,0)
#ifndef PCI_CLASS_ACCELERATOR_PROCESSING
#define PCI_CLASS_ACCELERATOR_PROCESSING	0x1200
#endif
#endif

#endif /* __BACKPORT_LINUX_PCI_IDS_H */
