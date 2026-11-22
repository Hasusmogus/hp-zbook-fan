// SPDX-License-Identifier: GPL-2.0
/*
 * hp_zbook_fan - fan RPM + fan mode for HP ZBook Studio G8 (board 8873)
 *
 * EC registers (from DSDT \_SB.PC00.LPCB.EC0 / \_TZ.GFVE / IETM.IMOK):
 *   0x2E FRDC  fan1 tach period, 0x35 FR2C fan2 tach period  (RPM = 245760/x)
 *   0x2F FTGC  fan1 target,      0x36 FT2C fan2 target       (EC-owned, read-only)
 *   0x2D AFAN  fan mode, low nibble fan1, high nibble fan2; EC accepts 0..2
 *              0 = auto (default curve), 1 = max boost, 2 = elevated
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/acpi.h>
#include <linux/dmi.h>

#define REG_AFAN 0x2D
static const u8 reg_rpm[2] = { 0x2E, 0x35 };
static const u8 reg_tgt[2] = { 0x2F, 0x36 };

static int mode = -1;	/* -1: leave EC as-is at load */
module_param(mode, int, 0444);
MODULE_PARM_DESC(mode, "fan mode to apply at load/resume: 0=auto 1=max 2=elevated");

static struct platform_device *pdev;
static int cur_mode = -1;

static long period_to_rpm(u8 v) { return (v == 0 || v == 0xFF) ? 0 : (0x3C000 + (v >> 1)) / v; }

static int set_mode(int m)
{
	int ret;
	u8 v;
	if (m < 0 || m > 2) return -EINVAL;
	ret = ec_write(REG_AFAN, m | (m << 4));
	if (ret) return ret;
	if (ec_read(REG_AFAN, &v) || v != (m | (m << 4))) return -EIO;
	cur_mode = m;
	return 0;
}

static umode_t zf_visible(const void *d, enum hwmon_sensor_types t, u32 attr, int ch)
{
	if (t == hwmon_fan && (attr == hwmon_fan_input || attr == hwmon_fan_target || attr == hwmon_fan_label))
		return 0444;
	return 0;
}

static int zf_read(struct device *dev, enum hwmon_sensor_types t, u32 attr, int ch, long *val)
{
	u8 v; int ret;
	ret = ec_read(attr == hwmon_fan_input ? reg_rpm[ch] : reg_tgt[ch], &v);
	if (ret) return ret;
	*val = period_to_rpm(v);
	return 0;
}

static const char *const labels[] = { "CPU Fan", "GPU Fan" };
static int zf_read_string(struct device *dev, enum hwmon_sensor_types t, u32 attr, int ch, const char **s)
{ *s = labels[ch]; return 0; }

static const struct hwmon_ops zf_ops = { .is_visible = zf_visible, .read = zf_read, .read_string = zf_read_string };
static const struct hwmon_channel_info *const zf_info[] = {
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT | HWMON_F_TARGET | HWMON_F_LABEL,
				HWMON_F_INPUT | HWMON_F_TARGET | HWMON_F_LABEL),
	NULL
};
static const struct hwmon_chip_info zf_chip = { .ops = &zf_ops, .info = zf_info };

static ssize_t fan_mode_show(struct device *dev, struct device_attribute *a, char *buf)
{
	u8 v;
	if (ec_read(REG_AFAN, &v)) return -EIO;
	return sysfs_emit(buf, "%u\n", v & 0x0F);
}
static ssize_t fan_mode_store(struct device *dev, struct device_attribute *a, const char *buf, size_t n)
{
	int m, ret;
	if (kstrtoint(buf, 0, &m)) return -EINVAL;
	ret = set_mode(m);
	return ret ? ret : n;
}
static DEVICE_ATTR_RW(fan_mode);
static struct attribute *zf_attrs[] = { &dev_attr_fan_mode.attr, NULL };
ATTRIBUTE_GROUPS(zf);

static int zf_probe(struct platform_device *p)
{
	struct device *hw;
	if (mode >= 0 && set_mode(mode)) dev_warn(&p->dev, "could not set mode %d\n", mode);
	hw = devm_hwmon_device_register_with_info(&p->dev, "hp_zbook_fan", NULL, &zf_chip, zf_groups);
	return PTR_ERR_OR_ZERO(hw);
}

static int zf_resume(struct device *dev)
{
	if (cur_mode >= 0) set_mode(cur_mode);
	return 0;
}
static DEFINE_SIMPLE_DEV_PM_OPS(zf_pm, NULL, zf_resume);

static struct platform_driver zf_driver = {
	.driver = { .name = "hp_zbook_fan", .pm = pm_sleep_ptr(&zf_pm) },
	.probe = zf_probe,
};

static const struct dmi_system_id zf_dmi[] = {
	{ .matches = { DMI_MATCH(DMI_BOARD_VENDOR, "HP"), DMI_MATCH(DMI_BOARD_NAME, "8873") } },
	{ }
};
MODULE_DEVICE_TABLE(dmi, zf_dmi);

static int __init zf_init(void)
{
	int ret;
	if (!dmi_check_system(zf_dmi)) return -ENODEV;
	ret = platform_driver_register(&zf_driver);
	if (ret) return ret;
	pdev = platform_device_register_simple("hp_zbook_fan", -1, NULL, 0);
	if (IS_ERR(pdev)) { platform_driver_unregister(&zf_driver); return PTR_ERR(pdev); }
	return 0;
}
static void __exit zf_exit(void)
{
	platform_device_unregister(pdev);
	platform_driver_unregister(&zf_driver);
}
module_init(zf_init);
module_exit(zf_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HP ZBook Studio G8 fan RPM and fan mode");
