// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kinetic KTD3136 / KTD3137 I2C backlight (ginkgo / Redmi Note 8).
 *
 * Hardware: PMI632 GPIO6 = HWEN, QUPV3 SE1 I2C @ 0x36.
 * Some units populate TI LM3697 at the same address; detect via ID 0x00.
 *
 * PWM Register 0x06 bit7 is inverted: 0 enables PWM dimming. Downstream
 * leaves PWM on and feeds PM6125 LPG. Without that PWM the LED current
 * stays at 0, so this driver disables PWM and uses the 11-bit I2C ratio.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

#define KTD3136_REG_ID		0x00
#define KTD3136_REG_MODE	0x02
#define KTD3136_REG_CONTROL	0x03
#define KTD3136_REG_RATIO_LSB	0x04
#define KTD3136_REG_RATIO_MSB	0x05
#define KTD3136_REG_PWM		0x06

#define KTD3136_ID		0x18
#define KTD3136_MODE_ON		0xc9	/* ILED_FS=20.2mA+, BL enable */
#define KTD3136_MODE_OFF	0x98	/* downstream off value */
#define KTD3136_CTRL_LINEAR	BIT(1)
#define KTD3136_PWM_DISABLE	0x9b	/* PWM off, CH1+CH2 like 0x1b */

#define LM3697_REG_BRT_MSB	0x22
#define LM3697_REG_BRT_LSB	0x23

#define KTD3136_MAX_BRIGHTNESS	2047
#define KTD3136_DEFAULT_BRIGHTNESS 1024

enum ktd3136_ic {
	IC_KTD3136,
	IC_LM3697,
};

struct ktd3136 {
	struct i2c_client *client;
	struct regmap *regmap;
	struct gpio_desc *enable_gpio;
	enum ktd3136_ic ic;
};

static const struct regmap_config ktd3136_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x24,
};

static int ktd3136_write(struct ktd3136 *ktd, unsigned int reg, unsigned int val)
{
	return regmap_write(ktd->regmap, reg, val);
}

static int ktd3136_set_ktd(struct ktd3136 *ktd, unsigned int brightness)
{
	int ret;

	if (!brightness)
		return ktd3136_write(ktd, KTD3136_REG_MODE, KTD3136_MODE_OFF);

	ret = ktd3136_write(ktd, KTD3136_REG_MODE, KTD3136_MODE_ON);
	if (ret)
		return ret;

	/* LSB must be programmed first; MSB latches the 11-bit value. */
	ret = ktd3136_write(ktd, KTD3136_REG_RATIO_LSB, brightness & 0x7);
	if (ret)
		return ret;

	return ktd3136_write(ktd, KTD3136_REG_RATIO_MSB, (brightness >> 3) & 0xff);
}

static int ktd3136_init_lm3697(struct ktd3136 *ktd);

static int ktd3136_set_lm3697(struct ktd3136 *ktd, unsigned int brightness)
{
	int ret;

	if (!brightness)
		return ktd3136_write(ktd, 0x24, 0x00);

	/*
	 * Probe runs before LCDB/panel power. The first brightness write
	 * is lost; a later sysfs echo is what testers used to turn the
	 * backlight on. Re-init on every non-zero update.
	 */
	ret = ktd3136_init_lm3697(ktd);
	if (ret)
		return ret;

	/* LSB then MSB; the 11-bit code latches on the second write. */
	ret = ktd3136_write(ktd, LM3697_REG_BRT_LSB, brightness & 0xff);
	if (ret)
		return ret;

	ret = ktd3136_write(ktd, LM3697_REG_BRT_MSB, (brightness >> 8) & 0x7);
	if (ret)
		return ret;

	return ktd3136_write(ktd, 0x24, 0x03);
}

static int ktd3136_update_status(struct backlight_device *bl)
{
	struct ktd3136 *ktd = bl_get_data(bl);
	unsigned int brightness = backlight_get_brightness(bl);

	if (ktd->ic == IC_KTD3136)
		return ktd3136_set_ktd(ktd, brightness);

	return ktd3136_set_lm3697(ktd, brightness);
}

static const struct backlight_ops ktd3136_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = ktd3136_update_status,
};

static int ktd3136_init_ktd(struct ktd3136 *ktd)
{
	int ret;
	unsigned int val;

	/* Downstream: 0x06=0x1b (PWM still enabled). Disable PWM for I2C-only. */
	ret = ktd3136_write(ktd, KTD3136_REG_PWM, KTD3136_PWM_DISABLE);
	if (ret)
		return ret;

	ret = regmap_update_bits(ktd->regmap, KTD3136_REG_CONTROL,
				 KTD3136_CTRL_LINEAR, KTD3136_CTRL_LINEAR);
	if (ret)
		return ret;

	ret = ktd3136_write(ktd, KTD3136_REG_MODE, KTD3136_MODE_ON);
	if (ret)
		return ret;

	ret = regmap_read(ktd->regmap, KTD3136_REG_MODE, &val);
	if (!ret)
		dev_info(&ktd->client->dev, "KTD3136 mode=0x%02x pwm=0x%02x\n",
			 val, KTD3136_PWM_DISABLE);

	return 0;
}

static int ktd3136_init_lm3697(struct ktd3136 *ktd)
{
	static const u8 init[][2] = {
		{ 0x10, 0x03 },
		{ 0x13, 0x01 },
		{ 0x16, 0x00 },
		{ 0x17, 0x19 },
		{ 0x18, 0x19 },
		{ 0x19, 0x03 },
		{ 0x1a, 0x0c },
		{ 0x1c, 0x0f },
		{ 0x24, 0x03 },
	};
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(init); i++) {
		ret = ktd3136_write(ktd, init[i][0], init[i][1]);
		if (ret)
			return ret;
	}

	return 0;
}

static int ktd3136_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct backlight_device *bl;
	struct backlight_properties props;
	struct ktd3136 *ktd;
	unsigned int id = 0, brightness = KTD3136_DEFAULT_BRIGHTNESS;
	int ret;

	ktd = devm_kzalloc(dev, sizeof(*ktd), GFP_KERNEL);
	if (!ktd)
		return -ENOMEM;

	ktd->client = client;
	ktd->regmap = devm_regmap_init_i2c(client, &ktd3136_regmap_config);
	if (IS_ERR(ktd->regmap))
		return dev_err_probe(dev, PTR_ERR(ktd->regmap), "regmap\n");

	ktd->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(ktd->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(ktd->enable_gpio),
				     "enable gpio\n");

	/* HWEN must be high before I2C is valid. */
	usleep_range(10000, 12000);

	ret = regmap_read(ktd->regmap, KTD3136_REG_ID, &id);
	if (ret)
		return dev_err_probe(dev, ret, "read device id @0x00\n");

	if (id == KTD3136_ID) {
		ktd->ic = IC_KTD3136;
		ret = ktd3136_init_ktd(ktd);
	} else {
		ktd->ic = IC_LM3697;
		dev_info(dev, "id=0x%02x, treating as LM3697\n", id);
		ret = ktd3136_init_lm3697(ktd);
	}
	if (ret)
		return dev_err_probe(dev, ret, "chip init\n");

	device_property_read_u32(dev, "default-brightness", &brightness);
	if (brightness > KTD3136_MAX_BRIGHTNESS)
		brightness = KTD3136_MAX_BRIGHTNESS;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = KTD3136_MAX_BRIGHTNESS;
	props.brightness = brightness;
	props.scale = BACKLIGHT_SCALE_LINEAR;

	bl = devm_backlight_device_register(dev, "ktd3136-backlight", dev, ktd,
					    &ktd3136_ops, &props);
	if (IS_ERR(bl))
		return dev_err_probe(dev, PTR_ERR(bl), "backlight register\n");

	i2c_set_clientdata(client, bl);
	backlight_update_status(bl);

	dev_info(dev, "probed id=0x%02x brightness=%u\n", id, brightness);
	return 0;
}

static void ktd3136_remove(struct i2c_client *client)
{
	struct backlight_device *bl = i2c_get_clientdata(client);

	bl->props.brightness = 0;
	backlight_update_status(bl);
}

static const struct i2c_device_id ktd3136_ids[] = {
	{ "ktd3136" },
	{}
};
MODULE_DEVICE_TABLE(i2c, ktd3136_ids);

static const struct of_device_id ktd3136_of_match[] = {
	{ .compatible = "kinetic,ktd3136" },
	{ .compatible = "ktd,ktd3136" },
	{}
};
MODULE_DEVICE_TABLE(of, ktd3136_of_match);

static struct i2c_driver ktd3136_driver = {
	.driver = {
		.name = "ktd3136",
		.of_match_table = ktd3136_of_match,
	},
	.probe = ktd3136_probe,
	.remove = ktd3136_remove,
	.id_table = ktd3136_ids,
};
module_i2c_driver(ktd3136_driver);

MODULE_DESCRIPTION("Kinetic KTD3136 backlight driver");
MODULE_LICENSE("GPL");
