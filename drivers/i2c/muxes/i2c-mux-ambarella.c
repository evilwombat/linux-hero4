/*
 * Ambarella I2C multiplexer
 *
 * Zhenwu Xue <zwxue@ambarella.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/i2c-mux-ambarella.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/gpio.h>

struct ambarella_mux {
	struct i2c_adapter			*parent;
	struct i2c_adapter			*adap; /* child busses */
	struct ambarella_i2cmux_platform_data	data;
};

static int ambarella_mux_select(struct i2c_adapter *adap, void *data, u32 chan)
{
	struct ambarella_mux			*mux = data;

	ambarella_gpio_config(mux->data.gpio, mux->data.select_function);

	return 0;
}

static int ambarella_mux_deselect(struct i2c_adapter *adap, void *data, u32 chan)
{
	struct ambarella_mux			*mux = data;

	ambarella_gpio_config(mux->data.gpio, mux->data.deselect_function);

	return 0;
}

static int ambarella_mux_probe(struct platform_device *pdev)
{
	struct ambarella_mux			*mux;
	struct ambarella_i2cmux_platform_data	*pdata;
	struct i2c_adapter			*parent;
	int					ret = 0;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "Missing platform data\n");
		return -ENODEV;
	}

	parent = i2c_get_adapter(pdata->parent);
	if (!parent) {
		dev_err(&pdev->dev, "Parent adapter (%d) not found\n",
			pdata->parent);
		return -ENODEV;
	}

	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux) {
		ret = -ENOMEM;
		goto alloc_failed;
	}

	mux->parent = parent;
	mux->data = *pdata;
	mux->adap = i2c_add_mux_adapter(parent, &pdev->dev, mux, pdata->number, 0,
		0, ambarella_mux_select, ambarella_mux_deselect);
	if (!mux->adap) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "Failed to add mux adapter\n");
		goto add_adapter_failed;
	}

	ambarella_mux_deselect(mux->adap, mux, 0);
	dev_info(&pdev->dev, "mux on %s adapter\n", parent->name);

	platform_set_drvdata(pdev, mux);

	goto ambarella_mux_probe_exit;

add_adapter_failed:
	i2c_del_mux_adapter(mux->adap);
	kfree(mux);

alloc_failed:
	i2c_put_adapter(parent);

ambarella_mux_probe_exit:
	return ret;
}

static int ambarella_mux_remove(struct platform_device *pdev)
{
	struct ambarella_mux			*mux;

	mux = platform_get_drvdata(pdev);
	if (mux) {
		i2c_del_mux_adapter(mux->adap);
		i2c_put_adapter(mux->parent);
		kfree(mux);
	}

	return 0;
}

static struct platform_driver ambarella_mux_driver = {
	.probe	= ambarella_mux_probe,
	.remove	= ambarella_mux_remove,
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "ambarella-i2cmux",
	},
};

static int __init ambarella_mux_init(void)
{
	return platform_driver_register(&ambarella_mux_driver);
}

static void __exit ambarella_mux_exit(void)
{
	platform_driver_unregister(&ambarella_mux_driver);
}

subsys_initcall(ambarella_mux_init);
module_exit(ambarella_mux_exit);

MODULE_DESCRIPTION("Ambarella I2C multiplexer driver");
MODULE_AUTHOR("Zhenwu Xue <zwxue@ambarella.com>");
MODULE_LICENSE("GPL");
