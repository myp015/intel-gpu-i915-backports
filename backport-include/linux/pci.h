#ifndef __BACKPORT_PCI_H
#define __BACKPORT_PCI_H
#include <linux/version.h>
#include_next <linux/pci.h>

#ifdef BPM_PCI_REBAR_SIZE_NOT_PRESENT
u32 pci_rebar_get_possible_sizes(struct pci_dev *pdev, int bar);

/*actually introduced in 5.12, for now keeping 5.10*/
static inline int pci_rebar_bytes_to_size(u64 bytes)
{
        bytes = roundup_pow_of_two(bytes);

        /* Return BAR size as defined in the resizable BAR specification */
        return max(ilog2(bytes), 20) - 20;
}

#endif /* BPM_PCI_REBAR_SIZE_NOT_PRESENT */

/*
 * Task#202：dev_is_removable() 依赖 5.13 才有的 struct device.removable。
 * 旧内核上用它判断 Thunderbolt 外接 GPU（5.10 内核自己的 amdgpu 代码
 * 也是用 pci_is_thunderbolt_attached() 做同一判断）。
 * 放在 pci.h 里是因为实现需要 pci_is_thunderbolt_attached()/to_pci_dev()。
 */
#if LINUX_VERSION_IS_LESS(5,13,0)
static inline bool dev_is_removable(struct device *dev)
{
	return dev_is_pci(dev) ? pci_is_thunderbolt_attached(to_pci_dev(dev)) : false;
}
#endif
#endif /* __BACKPORT_PCI_H */

