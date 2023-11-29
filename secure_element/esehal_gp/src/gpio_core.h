#ifndef GPIO_CORE_H
#define GPIO_CORE_H

#ifndef POLL_MODE
/* GPIO chipset for interrupt */
#define SE_GTO_GPIO_CHIP "/dev/" GPIO_CHIP_NAME

int gpio_interrupt_setup(struct se_gto_ctx *ctx);
int gpio_interrupt_teardown(struct se_gto_ctx *ctx);
void gpio_write(const char *dev_name, int offset, uint8_t value);
void gpio_read(const char *dev_name, int offset);
int gpio_poll(struct t1_state *t1, int timeout);
#endif

#endif /* GPIO_CORE_H */
