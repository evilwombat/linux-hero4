/*
 * leds-as3715.c - driver for GoPro Hero4 AS3715 PMIC / LED backlight controller
 * 
 * Derived from
 * 	leds-lp3944.c - driver for National Semiconductor LP3944 Funlight Chip
 * 			by Antonio Ospite <ospite@studenti.unina.it>
 *
 * Copyright (C) 2015, evilwombat
 * Copyright (C) 2009 Antonio Ospite <ospite@studenti.unina.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#define AS3715_BL_MAX_BRIGHTNESS	0x85

struct as3715_data {
	unsigned int brightness;
	struct mutex lock;
	struct i2c_client *client;
	struct led_classdev ldev;
	struct work_struct work;
};

static void as3715_init_regs(struct i2c_client *client)
{
#ifdef CONFIG_FB_ST7789
	i2c_smbus_write_byte_data(client, 0x00, 0xb0);
	i2c_smbus_write_byte_data(client, 0x01, 0xd0);
	i2c_smbus_write_byte_data(client, 0x02, 0xf8);
	i2c_smbus_write_byte_data(client, 0x03, 0x28);
	i2c_smbus_write_byte_data(client, 0x04, 0x9c);
	i2c_smbus_write_byte_data(client, 0x05, 0xda);
#endif
	i2c_smbus_write_byte_data(client, 0x06, 0x73);
#ifdef CONFIG_FB_ST7789
	i2c_smbus_write_byte_data(client, 0x07, 0x43);
	i2c_smbus_write_byte_data(client, 0x08, 0x43);
	i2c_smbus_write_byte_data(client, 0x09, 0x00);
	i2c_smbus_write_byte_data(client, 0x0a, 0x00);
	i2c_smbus_write_byte_data(client, 0x0b, 0x0e);
	i2c_smbus_write_byte_data(client, 0x0c, 0x8a);
	i2c_smbus_write_byte_data(client, 0x0d, 0x80);
	i2c_smbus_write_byte_data(client, 0x0e, 0x02);
	i2c_smbus_write_byte_data(client, 0x0f, 0x02);
	i2c_smbus_write_byte_data(client, 0x10, 0x0f);
	i2c_smbus_write_byte_data(client, 0x20, 0xc0);
	i2c_smbus_write_byte_data(client, 0x21, 0xc3);
	i2c_smbus_write_byte_data(client, 0x22, 0x00);
	i2c_smbus_write_byte_data(client, 0x23, 0x00);
	i2c_smbus_write_byte_data(client, 0x24, 0xff);
	i2c_smbus_write_byte_data(client, 0x25, 0x00);
	i2c_smbus_write_byte_data(client, 0x26, 0x00);
	i2c_smbus_write_byte_data(client, 0x27, 0x00);
	i2c_smbus_write_byte_data(client, 0x2b, 0x00);
	i2c_smbus_write_byte_data(client, 0x2c, 0x0f);
	i2c_smbus_write_byte_data(client, 0x2d, 0x00);
	i2c_smbus_write_byte_data(client, 0x2e, 0x00);
	i2c_smbus_write_byte_data(client, 0x30, 0x40);
	i2c_smbus_write_byte_data(client, 0x31, 0x04);
	i2c_smbus_write_byte_data(client, 0x32, 0x1d);
	i2c_smbus_write_byte_data(client, 0x33, 0x06);
	i2c_smbus_write_byte_data(client, 0x34, 0x18);
	i2c_smbus_write_byte_data(client, 0x35, 0x00);
	i2c_smbus_write_byte_data(client, 0x36, 0x00);
	i2c_smbus_write_byte_data(client, 0x37, 0x01);
	i2c_smbus_write_byte_data(client, 0x38, 0x00);
	i2c_smbus_write_byte_data(client, 0x39, 0x00);
	i2c_smbus_write_byte_data(client, 0x3a, 0x00);
	i2c_smbus_write_byte_data(client, 0x3b, 0x0f);
	i2c_smbus_write_byte_data(client, 0x3c, 0x9b);
	i2c_smbus_write_byte_data(client, 0x40, 0x02);
	i2c_smbus_write_byte_data(client, 0x41, 0x00);
	i2c_smbus_write_byte_data(client, 0x42, 0x00);
	i2c_smbus_write_byte_data(client, 0x43, 0x00);
	i2c_smbus_write_byte_data(client, 0x44, 0x2f);
	i2c_smbus_write_byte_data(client, 0x45, 0x01);
	i2c_smbus_write_byte_data(client, 0x46, 0x00);
	i2c_smbus_write_byte_data(client, 0x47, 0xff);
	i2c_smbus_write_byte_data(client, 0x48, 0x00);
	i2c_smbus_write_byte_data(client, 0x51, 0x19);
	i2c_smbus_write_byte_data(client, 0x53, 0x01);
	i2c_smbus_write_byte_data(client, 0x54, 0x00);
	i2c_smbus_write_byte_data(client, 0x55, 0x01);
	i2c_smbus_write_byte_data(client, 0x56, 0x76);
	i2c_smbus_write_byte_data(client, 0x60, 0x02);
	i2c_smbus_write_byte_data(client, 0x61, 0x18);
	i2c_smbus_write_byte_data(client, 0x63, 0x44);
	i2c_smbus_write_byte_data(client, 0x67, 0x00);
	i2c_smbus_write_byte_data(client, 0x69, 0x00);
	i2c_smbus_write_byte_data(client, 0x70, 0x1a);
	i2c_smbus_write_byte_data(client, 0x71, 0x00);
	i2c_smbus_write_byte_data(client, 0x72, 0x04);
	i2c_smbus_write_byte_data(client, 0x73, 0x00);
	i2c_smbus_write_byte_data(client, 0x74, 0xa5);
	i2c_smbus_write_byte_data(client, 0x75, 0x9b);
	i2c_smbus_write_byte_data(client, 0x76, 0x3e);
	i2c_smbus_write_byte_data(client, 0x7f, 0x09);
	i2c_smbus_write_byte_data(client, 0x80, 0xf3);
	i2c_smbus_write_byte_data(client, 0x81, 0x6a);
	i2c_smbus_write_byte_data(client, 0x82, 0x85);
	i2c_smbus_write_byte_data(client, 0x83, 0x4e);
	i2c_smbus_write_byte_data(client, 0x84, 0x4f);
	i2c_smbus_write_byte_data(client, 0x85, 0x67);
	i2c_smbus_write_byte_data(client, 0x86, 0x11);
	i2c_smbus_write_byte_data(client, 0x87, 0x82);
	i2c_smbus_write_byte_data(client, 0x88, 0x0f);
	i2c_smbus_write_byte_data(client, 0x89, 0x0c);
	i2c_smbus_write_byte_data(client, 0x8a, 0x11);
	i2c_smbus_write_byte_data(client, 0x8b, 0x12);
	i2c_smbus_write_byte_data(client, 0x8c, 0xba);
	i2c_smbus_write_byte_data(client, 0x8d, 0x36);
	i2c_smbus_write_byte_data(client, 0x8e, 0x00);
	i2c_smbus_write_byte_data(client, 0x90, 0x8d);
	i2c_smbus_write_byte_data(client, 0x91, 0x02);
#endif
}

static int as3715_led_set(struct i2c_client *client)
{
	int ret;
	struct as3715_data *dat = i2c_get_clientdata(client);
	mutex_lock(&dat->lock);
	
	if (dat->brightness > AS3715_BL_MAX_BRIGHTNESS)
		dat->brightness = AS3715_BL_MAX_BRIGHTNESS;
	
	ret = i2c_smbus_write_byte_data(client, 0x43, dat->brightness);

	mutex_unlock(&dat->lock);

	return ret < 0;
}

static void as3715_led_set_brightness(struct led_classdev *led_cdev,
				      enum led_brightness brightness)
{
	struct as3715_data *dat = container_of(led_cdev, struct as3715_data, ldev);

	dat->brightness = brightness;
	schedule_work(&dat->work);
}

static void as3715_work(struct work_struct *work)
{
	struct as3715_data *dat;
	dat = container_of(work, struct as3715_data, work);
	as3715_led_set(dat->client);
}

static int as3715_led_configure(struct i2c_client *client,
			    struct as3715_data *data)
{
	int err;
	data->ldev.name = "hero4-backlight";
	data->ldev.max_brightness = AS3715_BL_MAX_BRIGHTNESS;
	data->ldev.brightness_set = as3715_led_set_brightness;
	data->ldev.flags = LED_CORE_SUSPENDRESUME;
	data->brightness = 60;

	INIT_WORK(&data->work, as3715_work);

	err = led_classdev_register(&client->dev, &data->ldev);
	if (err < 0) {
		dev_err(&client->dev, "couldn't register backlight LED\n");
		goto exit;
	}

	/* to expose the default value to userspace */
	data->ldev.brightness = data->brightness;

	/* Set the default led status */
	err = as3715_led_set(client);
	if (err < 0) {
		dev_err(&client->dev, "couldn't set default backlight brightness\n");
		goto exit;
	}
	return 0;

exit:
	led_classdev_unregister(&data->ldev);
	cancel_work_sync(&data->work);

	return err;
}

static int as3715_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	struct as3715_data *data;
	int err;

	/* Let's see whether this adapter can support what we need. */
	if (!i2c_check_functionality(client->adapter, 
				I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "insufficient functionality!\n");
		return -ENODEV;
	}

	data = devm_kzalloc(&client->dev, sizeof(struct as3715_data),
			GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	i2c_set_clientdata(client, data);

	mutex_init(&data->lock);

	as3715_init_regs(client);
	
	err = as3715_led_configure(client, data);
	if (err < 0)
		return err;

	dev_info(&client->dev, "as3715 backlight controller initialized\n");
	return 0;
}

static int as3715_remove(struct i2c_client *client)
{
	struct as3715_data *data = i2c_get_clientdata(client);

	led_classdev_unregister(&data->ldev);
	cancel_work_sync(&data->work);
	
	return 0;
}

static const struct i2c_device_id as3715_id[] = {
	{"as3715", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, as3715_id);

static struct i2c_driver as3715_driver = {
	.driver   = {
		   .name = "as3715",
	},
	.probe    = as3715_probe,
	.remove   = as3715_remove,
	.id_table = as3715_id,
};

module_i2c_driver(as3715_driver);

MODULE_AUTHOR("evilwombat <evilwombat@server.fake>");
MODULE_DESCRIPTION("as3715 PMIC LED driver");
MODULE_LICENSE("GPL");
