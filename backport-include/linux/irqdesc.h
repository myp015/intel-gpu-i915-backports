#ifndef __BACKPORT_LINUX_IRQDESC_H
#define __BACKPORT_LINUX_IRQDESC_H
#include <linux/version.h>
#include_next <linux/irqdesc.h>

/*
 * Task#202：generic_handle_domain_irq() 是 5.11 引入的接口。
 * 5.10 上用 irq_find_mapping() + generic_handle_irq() 等价实现。
 */
#if LINUX_VERSION_IS_LESS(5,11,0)
#include <linux/irqdomain.h>

static inline int generic_handle_domain_irq(struct irq_domain *domain,
					    unsigned int hwirq)
{
	return generic_handle_irq(irq_find_mapping(domain, hwirq));
}
#endif

#endif /* __BACKPORT_LINUX_IRQDESC_H */
