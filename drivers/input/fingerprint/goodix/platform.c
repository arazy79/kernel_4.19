/*
 * platform indepent driver interface
 *
 * Coypritht (c) 2017 Goodix
 */
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/timer.h>
#include <linux/err.h>

#include "gf_spi.h"

#if defined(USE_SPI_BUS)
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#elif defined(USE_PLATFORM_BUS)
#include <linux/platform_device.h>
#endif

int gf_parse_dts(struct gf_dev *gf_dev)
{
	int rc = 0;
	struct device *dev = &gf_dev->spi->dev;
	struct device_node *np = dev->of_node;

	gf_dev->vdd_gpio = of_get_named_gpio(np, "goodix,gpio_vdd", 0);
	if (gf_dev->vdd_gpio < 0) {
		pr_err("falied to get vdd gpio!\n");
		rc = gf_dev->vdd_gpio;
		goto err_vdd;
	}
	rc = devm_gpio_request(dev, gf_dev->vdd_gpio, "goodix_vdd");
	if (rc) {
		pr_err("failed to request vdd gpio, rc = %d\n", rc);
		goto err_vdd;
	}
	gpio_direction_output(gf_dev->vdd_gpio, 1);

	gf_dev->reset_gpio = of_get_named_gpio(np, "goodix,reset_gpio", 0);
	if (gf_dev->reset_gpio < 0) {
		pr_err("falied to get reset gpio!\n");
		rc = gf_dev->reset_gpio;
		goto err_reset;
	}

	rc = devm_gpio_request(dev, gf_dev->reset_gpio, "goodix_reset");
	if (rc) {
		pr_err("failed to request reset gpio, rc = %d\n", rc);
		goto err_reset;
	}
	gpio_direction_output(gf_dev->reset_gpio, 1);

	gf_dev->irq_gpio = of_get_named_gpio(np, "goodix,irq_gpio", 0);
	if (gf_dev->irq_gpio < 0) {
		pr_err("falied to get irq gpio!\n");
		rc = gf_dev->irq_gpio;
		goto err_irq;
	}

	rc = devm_gpio_request(dev, gf_dev->irq_gpio, "goodix_irq");
	if (rc) {
		pr_err("failed to request irq gpio, rc = %d\n", rc);
		goto err_irq;
	}
	gpio_direction_input(gf_dev->irq_gpio);

	return 0;

err_irq:
	gf_dev->irq_gpio = -EINVAL;
	devm_gpio_free(dev, gf_dev->reset_gpio);
err_reset:
	gf_dev->reset_gpio = -EINVAL;
	devm_gpio_free(dev, gf_dev->vdd_gpio);
err_vdd:
	gf_dev->vdd_gpio = -EINVAL;
	return rc;
}

void gf_cleanup(struct gf_dev *gf_dev)
{
	struct device *dev = &gf_dev->spi->dev;

	pr_info("[info] %s\n", __func__);

	/* GPIOs are devm-requested and invalidated once freed, so this is
	 * safe to call more than once. */
	if (gpio_is_valid(gf_dev->irq_gpio)) {
		devm_gpio_free(dev, gf_dev->irq_gpio);
		gf_dev->irq_gpio = -EINVAL;
		pr_info("remove irq_gpio success\n");
	}
	if (gpio_is_valid(gf_dev->reset_gpio)) {
		devm_gpio_free(dev, gf_dev->reset_gpio);
		gf_dev->reset_gpio = -EINVAL;
		pr_info("remove reset_gpio success\n");
	}
	if (gpio_is_valid(gf_dev->vdd_gpio)) {
		devm_gpio_free(dev, gf_dev->vdd_gpio);
		gf_dev->vdd_gpio = -EINVAL;
		pr_info("remove vdd_gpio success\n");
	}
}

int gf_power_on(struct gf_dev *gf_dev)
{
	int rc = 0;

	/* TODO: add your power control here */
	return rc;
}

int gf_power_off(struct gf_dev *gf_dev)
{
	int rc = 0;

	/* TODO: add your power control here */

	return rc;
}

int gf_hw_reset(struct gf_dev *gf_dev, unsigned int delay_ms)
{
	int ret = -1;

	if (gf_dev == NULL) {
		pr_info("Input buff is NULL.\n");
		return ret;
	}
	if (!gpio_is_valid(gf_dev->reset_gpio))
		return -ENODEV;
	gpio_direction_output(gf_dev->reset_gpio, 1);
	gpio_set_value(gf_dev->reset_gpio, 0);
	mdelay(3);
	gpio_set_value(gf_dev->reset_gpio, 1);
	mdelay(delay_ms);
	return 0;
}

int gf_irq_num(struct gf_dev *gf_dev)
{
	int ret = -1;
	if (gf_dev == NULL) {
		pr_info("Input buff is NULL.\n");
		return ret;
	} else {
		return gpio_to_irq(gf_dev->irq_gpio);
	}
}
