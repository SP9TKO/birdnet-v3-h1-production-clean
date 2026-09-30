/*
 * SPDX-License-Identifier: Apache-2.0
 * Adapted from the pinned Zephyr Ethos-U device driver. The sole functional
 * adaptation is the qualified dedicated BASEH1 fast-memory binding.
 */

#include "h1_memory_contract.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <ethosu_driver.h>

LOG_MODULE_REGISTER(ethos_u, CONFIG_ARM_ETHOS_U_LOG_LEVEL);
#define DT_DRV_COMPAT arm_ethos_u

volatile uint32_t h1IrqCount;

void *ethosu_mutex_create(void)
{
	struct k_mutex *mutex = k_malloc(sizeof(*mutex));
	if (mutex == NULL) {
		return NULL;
	}
	k_mutex_init(mutex);
	return mutex;
}

int ethosu_mutex_lock(void *mutex)
{
	return k_mutex_lock((struct k_mutex *)mutex, K_FOREVER) == 0 ? 0 : -1;
}

int ethosu_mutex_unlock(void *mutex)
{
	k_mutex_unlock((struct k_mutex *)mutex);
	return 0;
}

void *ethosu_semaphore_create(void)
{
	struct k_sem *semaphore = k_malloc(sizeof(*semaphore));
	if (semaphore == NULL) {
		return NULL;
	}
	k_sem_init(semaphore, 0, 100);
	return semaphore;
}

int ethosu_semaphore_take(void *semaphore, uint64_t timeout)
{
	return k_sem_take((struct k_sem *)semaphore,
			  timeout == ETHOSU_SEMAPHORE_WAIT_FOREVER ? K_FOREVER
								    : Z_TIMEOUT_TICKS(timeout)) == 0
		       ? 0
		       : -1;
}

int ethosu_semaphore_give(void *semaphore)
{
	k_sem_give((struct k_sem *)semaphore);
	return 0;
}

struct ethosu_dts_info {
	void *base_addr;
	bool secure_enable;
	bool privilege_enable;
	void (*irq_config)(void);
};

struct ethosu_data {
	struct ethosu_driver drv;
};

void ethosu_zephyr_irq_handler(const struct device *device)
{
	struct ethosu_data *data = device->data;
	++h1IrqCount;
	ethosu_irq_handler(&data->drv);
}

static int ethosu_zephyr_init(const struct device *device)
{
	const struct ethosu_dts_info *config = device->config;
	struct ethosu_data *data = device->data;
	if (ethosu_init(&data->drv, config->base_addr, (void *)H1_FAST_ADDRESS,
			H1_FAST_RESERVATION_BYTES, config->secure_enable,
			config->privilege_enable)) {
		return -EINVAL;
	}
	config->irq_config();
	return 0;
}

#define ETHOSU_DEVICE_INIT(n)                                                        \
	static struct ethosu_data ethosu_data_##n;                                    \
	static void ethosu_zephyr_irq_config_##n(void)                                \
	{                                                                              \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority),                  \
			    ethosu_zephyr_irq_handler, DEVICE_DT_INST_GET(n), 0);        \
		irq_enable(DT_INST_IRQN(n));                                            \
	}                                                                              \
	static const struct ethosu_dts_info ethosu_dts_info_##n = {                    \
		.base_addr = (void *)DT_INST_REG_ADDR(n),                               \
		.secure_enable = DT_INST_PROP(n, secure_enable),                        \
		.privilege_enable = DT_INST_PROP(n, privilege_enable),                  \
		.irq_config = &ethosu_zephyr_irq_config_##n,                            \
	};                                                                             \
	DEVICE_DT_INST_DEFINE(n, ethosu_zephyr_init, NULL, &ethosu_data_##n,            \
			      &ethosu_dts_info_##n, POST_KERNEL,                           \
			      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, NULL);

DT_INST_FOREACH_STATUS_OKAY(ETHOSU_DEVICE_INIT)
