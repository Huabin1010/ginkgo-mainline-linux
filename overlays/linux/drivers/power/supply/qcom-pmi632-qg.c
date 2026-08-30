// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal PMI632 Qualcomm Gauge: voltage_now / current_now / capacity
 * from QG LAST_ADC (no full CAF qpnp-qg SOC algorithm).
 */
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/errno.h>
#include <linux/math64.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>

#define QG_PERPH_TYPE			0x04
#define QG_PERPH_SUBTYPE		0x05
#define QG_TYPE				0x0D
#define QG_ADC_IBAT_5A			0x03
#define QG_ADC_IBAT_10A			0x04
#define QG_STATUS1			0x08
#define QG_OK_BIT			BIT(7)
#define QG_BATT_PRESENT_BIT		BIT(0)
#define QG_DATA_CTL2			0x42
#define QG_BURST_AVG_HOLD		BIT(0)
#define QG_STATUS3			0x0A
#define QG_FIFO_RT_MASK			GENMASK(3, 0)
#define QG_S2_AVG_V			0x80
#define QG_S2_AVG_I			0x82
#define QG_I_FIFO0			0xA0
#define QG_SOC_MONOTONIC		0xBF
#define QG_LAST_ADC_V			0xC0
#define QG_LAST_ADC_I			0xC2
#define QG_LAST_BURST_AVG_I		0xC6
#define QG_FIFO_I_RESET			0x8000
#define QG_FIFO_MAX			8

/* CAF qg-defs.h V_RAW_TO_UV / I_RAW_TO_UA */
#define QG_V_LSB_UV			194637
#define QG_I_LSB_5A_UA			152588
#define QG_I_LSB_10A_UA			305176

#define VBAT_MIN_UV			3400000
#define VBAT_MAX_UV			4350000

struct pmi632_qg {
	struct device *dev;
	struct regmap *regmap;
	u32 base;
	u32 i_lsb_ua;
	struct power_supply *psy;
};

static int qg_read8(struct pmi632_qg *qg, u32 off, u8 *val)
{
	unsigned int v;
	int ret;

	ret = regmap_read(qg->regmap, qg->base + off, &v);
	if (ret)
		return ret;
	*val = v;
	return 0;
}

static int qg_read16(struct pmi632_qg *qg, u32 off, u16 *val)
{
	u8 buf[2];
	int ret;

	ret = regmap_bulk_read(qg->regmap, qg->base + off, buf, 2);
	if (ret)
		return ret;
	*val = buf[0] | ((u16)buf[1] << 8);
	return 0;
}

static int qg_raw_to_ua(struct pmi632_qg *qg, u16 iraw)
{
	int sraw = sign_extend32(iraw, 15);

	return (int)div64_s64((s64)sraw * qg->i_lsb_ua, 1000);
}

static bool qg_iraw_ok(u16 iraw)
{
	return iraw && iraw != QG_FIFO_I_RESET;
}

static int qg_read_fifo_i(struct pmi632_qg *qg, int *ua)
{
	u8 nfifo = 0;
	u16 iraw = 0;
	s64 acc = 0;
	int i, n = 0, ret;

	ret = qg_read8(qg, QG_STATUS3, &nfifo);
	if (ret)
		return ret;
	nfifo &= QG_FIFO_RT_MASK;
	if (nfifo > QG_FIFO_MAX)
		nfifo = QG_FIFO_MAX;
	if (!nfifo)
		nfifo = 1;

	for (i = 0; i < nfifo; i++) {
		ret = qg_read16(qg, QG_I_FIFO0 + i * 2, &iraw);
		if (ret)
			return ret;
		if (!qg_iraw_ok(iraw))
			continue;
		acc += qg_raw_to_ua(qg, iraw);
		n++;
	}
	if (!n)
		return -ENODATA;
	*ua = (int)div64_s64(acc, n);
	return 0;
}

static int qg_read_ibat(struct pmi632_qg *qg, int *ua)
{
	u16 iraw = 0;
	int ret;

	/* CAF qpnp-qg.c: I FIFO on fifo-done, then HOLD + LAST_BURST_AVG_I. */
	if (!qg_read_fifo_i(qg, ua))
		return 0;

	ret = qg_read16(qg, QG_S2_AVG_I, &iraw);
	if (!ret && qg_iraw_ok(iraw)) {
		*ua = qg_raw_to_ua(qg, iraw);
		return 0;
	}

	regmap_update_bits(qg->regmap, qg->base + QG_DATA_CTL2,
			   QG_BURST_AVG_HOLD, QG_BURST_AVG_HOLD);
	ret = qg_read16(qg, QG_LAST_BURST_AVG_I, &iraw);
	regmap_update_bits(qg->regmap, qg->base + QG_DATA_CTL2,
			   QG_BURST_AVG_HOLD, 0);
	if (!ret && qg_iraw_ok(iraw)) {
		*ua = qg_raw_to_ua(qg, iraw);
		return 0;
	}

	ret = qg_read16(qg, QG_LAST_ADC_I, &iraw);
	if (ret)
		return ret;
	if (!qg_iraw_ok(iraw)) {
		*ua = 0;
		return 0;
	}
	*ua = qg_raw_to_ua(qg, iraw);
	return 0;
}

static int qg_last_vi(struct pmi632_qg *qg, int *uv, int *ua)
{
	u16 vraw = 0;
	int ret;

	ret = qg_read16(qg, QG_LAST_ADC_V, &vraw);
	if (ret)
		return ret;
	if (!(vraw & 0x7fff)) {
		ret = qg_read16(qg, QG_S2_AVG_V, &vraw);
		if (ret)
			return ret;
	}
	*uv = (int)div64_s64((s64)(vraw & 0x7fff) * QG_V_LSB_UV, 1000);
	return qg_read_ibat(qg, ua);
}

static int qg_capacity_from_v(int uv)
{
	int span = VBAT_MAX_UV - VBAT_MIN_UV;
	int pct;

	if (uv <= VBAT_MIN_UV)
		return 0;
	if (uv >= VBAT_MAX_UV)
		return 100;
	pct = (int)div64_s64((s64)(uv - VBAT_MIN_UV) * 100, span);
	return clamp(pct, 0, 100);
}

static int qg_get_property(struct power_supply *psy,
			   enum power_supply_property psp,
			   union power_supply_propval *val)
{
	struct pmi632_qg *qg = power_supply_get_drvdata(psy);
	int uv = 0, ua = 0, ret;
	u8 soc, st;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		ret = qg_read8(qg, QG_STATUS1, &st);
		if (ret)
			return ret;
		val->intval = !!(st & QG_BATT_PRESENT_BIT) || (st & QG_OK_BIT);
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_CAPACITY:
	case POWER_SUPPLY_PROP_STATUS:
		ret = qg_last_vi(qg, &uv, &ua);
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = uv;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = ua;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		if (!qg_read8(qg, QG_SOC_MONOTONIC, &soc) && soc > 0 && soc <= 100)
			val->intval = soc;
		else
			val->intval = qg_capacity_from_v(uv);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (ua > 50000)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (ua < -50000)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static enum power_supply_property qg_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

static const struct power_supply_desc qg_desc = {
	.name = "qcom-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = qg_props,
	.num_properties = ARRAY_SIZE(qg_props),
	.get_property = qg_get_property,
};

static int pmi632_qg_probe(struct platform_device *pdev)
{
	struct pmi632_qg *qg;
	struct power_supply_config psy_cfg = {};
	u8 type = 0;
	int ret;

	qg = devm_kzalloc(&pdev->dev, sizeof(*qg), GFP_KERNEL);
	if (!qg)
		return -ENOMEM;

	qg->dev = &pdev->dev;
	qg->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!qg->regmap)
		return dev_err_probe(&pdev->dev, -ENODEV, "no parent regmap\n");

	ret = of_property_read_u32(pdev->dev.of_node, "reg", &qg->base);
	if (ret)
		return ret;

	ret = qg_read8(qg, QG_PERPH_TYPE, &type);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "QG type read\n");
	if (type != QG_TYPE)
		dev_warn(&pdev->dev, "unexpected QG type 0x%02x (want 0x0d)\n",
			 type);

	ret = qg_read8(qg, QG_PERPH_SUBTYPE, &type);
	if (!ret && type == QG_ADC_IBAT_10A)
		qg->i_lsb_ua = QG_I_LSB_10A_UA;
	else
		qg->i_lsb_ua = QG_I_LSB_5A_UA;

	/* Drop a leftover burst-avg hold from a previous kernel. */
	regmap_update_bits(qg->regmap, qg->base + QG_DATA_CTL2,
			   QG_BURST_AVG_HOLD, 0);
	regmap_update_bits(qg->regmap, qg->base + 0x41, BIT(0), 0);

	psy_cfg.drv_data = qg;
	psy_cfg.fwnode = dev_fwnode(&pdev->dev);
	qg->psy = devm_power_supply_register(&pdev->dev, &qg_desc, &psy_cfg);
	if (IS_ERR(qg->psy))
		return PTR_ERR(qg->psy);

	platform_set_drvdata(pdev, qg);
	dev_info(&pdev->dev, "PMI632 QG at 0x%x subtype-lsb %u\n",
		 qg->base, qg->i_lsb_ua);
	return 0;
}

static const struct of_device_id pmi632_qg_of_match[] = {
	{ .compatible = "qcom,pmi632-qg" },
	{ }
};
MODULE_DEVICE_TABLE(of, pmi632_qg_of_match);

static struct platform_driver pmi632_qg_driver = {
	.probe = pmi632_qg_probe,
	.driver = {
		.name = "qcom-pmi632-qg",
		.of_match_table = pmi632_qg_of_match,
	},
};
module_platform_driver(pmi632_qg_driver);

MODULE_DESCRIPTION("Qualcomm PMI632 QG voltage/current");
MODULE_LICENSE("GPL");
