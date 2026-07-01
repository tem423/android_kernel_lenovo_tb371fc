#include <linux/init.h>
#include <linux/extcon-provider.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/usb/typec.h>
#include <linux/version.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/of_platform.h>
#include <linux/workqueue.h>
#include <linux/iio/consumer.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/suspend.h>
#include <linux/of_irq.h>
#include <linux/pm_wakeup.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/alarmtimer.h>
#include <linux/power_supply.h>
#include <v1/mtk_charger.h>
#include <v1/hardinfo_charger.h>
#include<linux/iio/iio.h>

#define USB_THERMAL_INTERVAL (3000)

const char * const hardinfo_vendor_chg_text[] = {
    "mt6360",
    "sgm41513",
    "sc8560",
    "sc8549",
    "sgm41600",
    "SCUD_4V48_10200MAH",
    "SUNWODA_4V48_10200MAH"
};

const char * const hardinfo_chg_type_text[] = {
    "UNKNOWN",
    "USB",
    "CDP",
    "FLOAT",
    "DCP",
    "PD_PPS",
    "PD20",
    "PEPLUS20"
};

struct hardinfo_data {
    const int version;
    int main_chg;
    int buck_chg;
    int chg_pump;
    int gauge;
    int chg_type;
/* Spruce code for OSPURCET-344 by zhangjb18 at 2022.12.07 start */
    int hiz_en;
/* Spruce code for OSPURCET-344 by zhangjb18 at 2022.12.07 end */
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 start */
    int chg_online;
    int charging;
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 end */
/* Spruce code for OSPURCET-33 by zhangjb18 at 2022.12.12 start */
    bool bm_flag;
    bool bp_flag;
    int fake_cycle;
    int fake_temp;
/* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 start */
    int thermal_limit_level;
    int thermal_levels;
/* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 end */
};

struct hw_bm_aging {
    int chg_cv;
    int bm_cv;
};

struct hw_bm_aging hw_aging_table[] = {
    [HW_LEVEL_0] = {4480000, 4480000},
    [HW_LEVEL_1] = {4430000, 4400000},
    [HW_LEVEL_2] = {4380000, 4350000},
    [HW_LEVEL_3] = {4250000, 4200000}
};

struct hardinfo_charger {
    struct power_supply *gpsy;
    struct hardinfo_data *hwinfo_data;
    /* Spruce code for OSPURCET-39 by liugong2 at 20221207 start */
    /*usb burning ntc adc*/
    int usb_mos_gpio;
    struct iio_channel *usb_burning_adc;
    struct delayed_work check_usb_thermal_work;
    /* private */
    struct device *dev;
    /* Spruce code for OSPURCET-39 by liugong2 at 20221207 end */
    /* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 start */
	int *thermal_mitigation_20w;
	int *thermal_mitigation_30w;
    /* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 end */
};

/* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 start */
static struct hardinfo_data _hwinfo_data = {-1, -1, -1, -1, -1, -1, 0, 1, 1, 0, 0, 0, -1, 0, 0};
/* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 end */
static struct hardinfo_charger *_hwinfo;
/* Spruce code for OSPURCET-33 by zhangjb18 at 2022.12.12 end */


/* Spruce code for OSPURCET-1932 by xuyc12 at 2023.03.07 start */
static int bp_level = 0;
enum {
    BP_UNWORK,
    CAPCAITY_ABOVE_60,
    CAPCAITY_40_TO_60,
    CAPCAITY_BELOW_40,
};

bool bq_input_stop = false;
bool bq_charger_stop = false;
/* Spruce code for OSPURCET-1932 by xuyc12 at 2023.03.07 end */

/*****************interface for hardinfo******************/
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 start */
static void hardinfo_set_chg_online(bool en)
{
    _hwinfo_data.chg_online = en;
    pr_err("%s %d\n", __func__, en);
}
int hardinfo_get_chg_online(void)
{
    return _hwinfo_data.chg_online;
}
void hardinfo_set_charging(bool en)
{
    _hwinfo_data.charging = en;
    pr_err("%s %d\n", __func__, en);
}
int hardinfo_get_charging(void)
{
    return _hwinfo_data.charging;
}
struct charger_device *hardinfo_get_chg_dev(void)
{
	static struct charger_device *chg_dev;

	if (IS_ERR_OR_NULL(chg_dev)) {
		chg_dev = get_charger_by_name("primary_chg");
        if(!IS_ERR_OR_NULL(chg_dev))
            return chg_dev;
    } else {
        return chg_dev;
    }

    return NULL;
}

#if IS_ENABLED(CONFIG_MTK_DUAL_CHARGER_SUPPORT)
struct charger_device *hardinfo_get_sec_chg_dev(void)
{
	static struct charger_device *chg_dev;

	if (IS_ERR_OR_NULL(chg_dev)) {
		chg_dev = get_charger_by_name("secondary_chg");
        if(!IS_ERR_OR_NULL(chg_dev))
            return chg_dev;
    } else {
        return chg_dev;
    }

    return NULL;
}
#endif

struct power_supply *hardinfo_get_cp_dev(void)
{
    int id;
	static struct power_supply *cp_psy;

	if (IS_ERR_OR_NULL(cp_psy)) {
        id = hardinfo_get_vendor_chginfo(HRADINFO_CHG_PUMP_INFO);
        switch (id)
        {
        case SC8549:
            cp_psy = power_supply_get_by_name("sc854x-standalone");
            break;
        case SGM41600:
            cp_psy = power_supply_get_by_name("sgm41600-standalone");
            break;
        default:
        break;
        }
        if(!IS_ERR_OR_NULL(cp_psy))
            return cp_psy;
    } else {
        return cp_psy;
    }

    return NULL;
}

static int hardinfo_set_hiz(bool en)
{
    int ret, temp_tf;
    union power_supply_propval val = {0,};
    struct power_supply *cp_psy;
    struct charger_device *chg_dev;
#if IS_ENABLED(CONFIG_MTK_DUAL_CHARGER_SUPPORT)
    struct charger_device *chg_dev2;
#endif

    /* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 start */
    temp_tf = (en ? false : true);
    usbpd_pm_set_cp_status(temp_tf);
    /* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 start */

    chg_dev = hardinfo_get_chg_dev();
    if (IS_ERR_OR_NULL(chg_dev))
        return -ENODEV;
    charger_dev_enable_hz(chg_dev, en);

    _hwinfo_data.chg_type = mtk_chr_get_type();
    if(_hwinfo_data.chg_type == PD30_PPS_CHARGER) {
        cp_psy = hardinfo_get_cp_dev();
        if (!IS_ERR_OR_NULL(cp_psy)) {
            val.intval = temp_tf;
            ret = power_supply_set_property(cp_psy,
                POWER_SUPPLY_PROP_ONLINE, &val);
            if(!ret)
                hardinfo_set_chg_online(temp_tf);
        }
    }

#if IS_ENABLED(CONFIG_MTK_DUAL_CHARGER_SUPPORT)
    chg_dev2 = hardinfo_get_sec_chg_dev();
    if (IS_ERR_OR_NULL(chg_dev2))
        return -ENODEV;
    charger_dev_enable_hz(chg_dev2, en);
    hardinfo_set_chg_online(temp_tf);
#endif

    return 0;
}

static int hardinfo_set_charging_enabled(bool en)
{
    struct charger_device *chg_dev1;
#if IS_ENABLED(CONFIG_MTK_DUAL_CHARGER_SUPPORT)
    struct charger_device *chg_dev2;
#endif
    struct power_supply *cp_psy;
    union power_supply_propval val = {0,};
    int ret;

    /* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 start */
    usbpd_pm_set_cp_status(en);
    /* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 end */

    chg_dev1 = hardinfo_get_chg_dev();
    if (IS_ERR_OR_NULL(chg_dev1))
        return -ENODEV;
    charger_dev_enable(chg_dev1, en);

    _hwinfo_data.chg_type = mtk_chr_get_type();
    switch (_hwinfo_data.chg_type)
    {
    case PD30_PPS_CHARGER:
        cp_psy = hardinfo_get_cp_dev();
        if (!IS_ERR_OR_NULL(cp_psy)) {
            val.intval = en;
            ret = power_supply_set_property(cp_psy,
                POWER_SUPPLY_PROP_ONLINE, &val);
            if (!ret)
                hardinfo_set_charging(en);
        }
        break;
    case PD20_CHARGER...PEPLUS20_CHARGER:
    #if IS_ENABLED(CONFIG_MTK_DUAL_CHARGER_SUPPORT)
        chg_dev2 = hardinfo_get_sec_chg_dev();
        if (IS_ERR_OR_NULL(chg_dev2))
            return -ENODEV;
        charger_dev_enable(chg_dev2, en);
        hardinfo_set_charging(en);
    #endif
        break;
    default:
        break;
    }

    return 0;
}

static int hardinfo_get_vbus(void)
{
    u32 vbus = 0;
    struct charger_device *chg_dev = hardinfo_get_chg_dev();
    if (IS_ERR_OR_NULL(chg_dev))
        return -ENODEV;

    charger_dev_get_vbus(chg_dev, &vbus);
    return vbus;
}

static int hardinfo_get_ibus(void)
{
    u32 ibus = 0;
    struct charger_device *chg_dev = hardinfo_get_chg_dev();
    if (IS_ERR_OR_NULL(chg_dev))
        return -ENODEV;

    charger_dev_get_ibus(chg_dev, &ibus);
    return ibus;
}
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 end */
/* Spruce code for OSPURCET-33 by zhangjb18 at 2022.12.12 start */
bool hardinfo_get_bm(void)
{
    return _hwinfo_data.bm_flag;
}

bool hardinfo_get_bp(void)
{
    return _hwinfo_data.bp_flag;
}
/* Spruce code for OSPURCET-1932 by xuyc12 at 2023.03.07 */
EXPORT_SYMBOL_GPL(hardinfo_get_bp);

int hardinfo_get_fake_temp(void)
{
    return _hwinfo_data.fake_temp;
}

/* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 start */
int hardinfo_get_thermal_limit(void)
{
    int thermal_limit_current;
    if(IS_ERR_OR_NULL(_hwinfo) || IS_ERR_OR_NULL(_hwinfo->thermal_mitigation_20w) ||
        IS_ERR_OR_NULL(_hwinfo->thermal_mitigation_30w))
        return -1;

    if(usbpd_pm_get_cp_status()) {
        thermal_limit_current = _hwinfo->thermal_mitigation_30w[_hwinfo_data.thermal_limit_level];
    } else {
        thermal_limit_current = _hwinfo->thermal_mitigation_20w[_hwinfo_data.thermal_limit_level];
    }

    return thermal_limit_current;
}

/* Spruce code for OSPURCET-463 by zhangjb18 at 2022.12.28 start */
static int hardinfo_get_charging_current(int id)
{
    bool chip2_en, cp_en;
    int ibat1, ibat2, ret;
    union power_supply_propval prop1, prop2;
    struct charger_device *chg_dev, *chg_dev2;
    struct power_supply *cp_psy;

    chg_dev = hardinfo_get_chg_dev();
    if (IS_ERR_OR_NULL(chg_dev) || IS_ERR_OR_NULL(_hwinfo))
        return -ENODEV;
    charger_dev_get_ibat(chg_dev, &ibat1);

    chg_dev2 = hardinfo_get_sec_chg_dev();
    if (IS_ERR_OR_NULL(chg_dev2) || IS_ERR_OR_NULL(_hwinfo->gpsy))
        return -ENODEV;
    charger_dev_is_enabled(chg_dev2, &chip2_en);

    ret = power_supply_get_property(_hwinfo->gpsy, POWER_SUPPLY_PROP_CURRENT_NOW, &prop1);
    if(!ret) {
        ibat2 = prop1.intval;
    }

    cp_psy = hardinfo_get_cp_dev();
    if (!IS_ERR_OR_NULL(cp_psy)) {
        ret = power_supply_get_property(cp_psy,
            POWER_SUPPLY_PROP_ONLINE, &prop2);
        if(!ret)
            cp_en = prop2.intval;
    }

    switch (id)
    {
    case HRADINFO_MAIN_CHARGING_CURRENT:
        return ibat1;
        break;
    case HRADINFO_SLAVE_CHARGING_CURRENT:
        if(chip2_en) {
            return (ibat2 - ibat1);
        } else {
            return 0;
        }
        break;
    case HRADINFO_CHGPUMP_CHARGING_CURRENT:
        if(cp_en) {
            return (ibat2 - ibat1);
        } else {
            return 0;
        }
        break;
    default:
        break;
    }

    return 0;
}
/* Spruce code for OSPURCET-463 by zhangjb18 at 2022.12.28 end */
/* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 end */

/* Spruce code for OSPURCET-33 by zhangjb18 at 2022.12.12 end */
/*****************interface for hardinfo******************/

/* Spruce code for OSPURCET-39 by liugong2 at 20221207 start */
static int get_usb_ntc_temp(struct hardinfo_charger *hinfo, int *temp_val)
{
    int ret = 0;

	/* Read processed value -  ep. usb_temp = 25 */
	ret = iio_read_channel_processed(hinfo->usb_burning_adc, temp_val);
	if (ret < 0){
        dev_err(hinfo->dev, "Failed to read processed value!!!");
        return ret;
    }

    dev_info(hinfo->dev, "The USB NTC TEMP is: %d\n", *temp_val);

    return ret;
}
/* Spruce code for OSPURCET-39 by liugong2 at 20221207 end */

int hardinfo_get_vendor_chginfo(enum hardware_id id)
{
    int chg_info = HARDINFO_ERROR;
    switch (id)
    {
    case HRADINFO_MAIN_BUCK_INFO:
        chg_info = _hwinfo_data.main_chg;
        break;
    case HRADINFO_BUCK_CHG_INFO:
        chg_info = _hwinfo_data.buck_chg;
        break;
    case HRADINFO_CHG_PUMP_INFO:
        chg_info = _hwinfo_data.chg_pump;
        break;
    case HRADINFO_GAUGE_INFO:
        chg_info = _hwinfo_data.gauge;
        break;
    default:
        chg_info = HARDINFO_ERROR;
        break;
    }

    return chg_info;
}
EXPORT_SYMBOL_GPL(hardinfo_get_vendor_chginfo);

void hardinfo_set_vendor_chginfo(enum hardware_id id, enum hardinfo_vendor_chg_info data)
{
    if (HARDINFO_ERROR == data) {
        pr_err("[HWINFO] %s the data of hwid %d is ERR\n", __func__, id);
    } else {
        switch (id) {
        case HRADINFO_MAIN_BUCK_INFO:
            _hwinfo_data.main_chg = data;
            break;
        case HRADINFO_BUCK_CHG_INFO:
            _hwinfo_data.buck_chg = data;
            break;
        case HRADINFO_CHG_PUMP_INFO:
            _hwinfo_data.chg_pump = data;
            break;
        case HRADINFO_GAUGE_INFO:
            _hwinfo_data.gauge = data;
        default:
            pr_err("[HWINFO] %s Invalid HWID\n", __func__);
            break;
        }
    }
}
EXPORT_SYMBOL(hardinfo_set_vendor_chginfo);

ssize_t gauge_show_attrs(struct device *dev,
                                struct device_attribute *attr, char *buf);
ssize_t gauge_store_attrs(struct device *dev,
                                struct device_attribute *attr, const char *buf, size_t count);

#define gauge_ATTR(_name)                              \
{                                                      \
    .attr = {.name = #_name, .mode = 0644},        \
    .show = gauge_show_attrs,                      \
    .store = gauge_store_attrs,                    \
}

static struct device_attribute gauge_attrs[] = {
    gauge_ATTR(primary_chg_info),
    gauge_ATTR(secondary_chg_info),
    gauge_ATTR(chg_pump_info),
    gauge_ATTR(gauge_info),
    gauge_ATTR(real_type),
    gauge_ATTR(hiz_en),
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 start */
    gauge_ATTR(charging_enabled),
    gauge_ATTR(vbus_now),
    gauge_ATTR(ibus_now),
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 end */
/* Spruce code for OSPURCET-33 by zhangjb18 at 2022.12.12 start */
    gauge_ATTR(en_bm),
    gauge_ATTR(en_bp),
    gauge_ATTR(fake_cc),
    gauge_ATTR(fake_temp),
/* Spruce code for OSPURCET-33 by zhangjb18 at 2022.12.12 end */
/* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 start */
    gauge_ATTR(charge_control_limit),
    gauge_ATTR(charge_control_limit_max),
/* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 end */
/* Spruce code for OSPURCET-463 by zhangjb18 at 2022.12.28 start */
    gauge_ATTR(primary_chg_current),
    gauge_ATTR(secondary_chg_current),
    gauge_ATTR(pumping_chg_current),
/* Spruce code for OSPURCET-463 by zhangjb18 at 2022.12.28 end */
};

ssize_t gauge_store_attrs(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    const ptrdiff_t offset = attr - gauge_attrs;
    unsigned int value;
    int ret = count;

    if(IS_ERR_OR_NULL(buf) || kstrtouint(buf, 10, &value))
        return -ENXIO;

/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 start */
    switch (offset) {
    case HRADINFO_HIZ_EN:
        if(value) {
            _hwinfo_data.hiz_en = true;
            /* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 start */
            hardinfo_set_charging_enabled(false);
            /* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 end */
            hardinfo_set_hiz(true);
        } else {
            _hwinfo_data.hiz_en = false;
            hardinfo_set_hiz(false);
            /* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 start */
            hardinfo_set_charging_enabled(true);
            /* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 end */
        }
        break;
    case HRADINFO_CHARGING_ENABLED:
        if(value) {
            hardinfo_set_charging_enabled(true);
            _hwinfo_data.charging = value;
        } else {
            hardinfo_set_charging_enabled(false);
            _hwinfo_data.charging = value;
        }
        break;
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 end */
/* Spruce code for OSPURCET-33 by zhangjb18 at 2022.12.12 start */
    case BATTERY_MAINTENANCE_ENABLE:
        pr_info("BATTERY_MAINTENANCE is set [%d]\n", value);
        if (value) {
            _hwinfo_data.bm_flag = true;
        } else {
            _hwinfo_data.bm_flag = false;
        }
        break;
    case BATTERY_PROTECTION_ENABLE:
        pr_info("BATTERY_PROTECTION is set [%d]\n", value);
        if (value) {
            _hwinfo_data.bp_flag = true;
        } else {
            _hwinfo_data.bp_flag = false;
        }
        break;
    case BATTERY_MAINTENANCE_FAKE_CYCLE:
        _hwinfo_data.fake_cycle = value;
        break;
    case BATTERY_FAKE_TEMP:
        _hwinfo_data.fake_temp = value;
        break;
/* Spruce code for OSPURCET-33 by zhangjb18 at 2022.12.12 end */
    /* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 start */
    case BATTERY_THERMAL_CONTROL_LIMIT:
        if(value < 0 || _hwinfo_data.thermal_levels <=0)
            return -EINVAL;

        if(value >= _hwinfo_data.thermal_levels)
            return -EINVAL;

        _hwinfo_data.thermal_limit_level = value;
        break;
    /* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 end */
    default:
    break;
    }

    return ret;
}

ssize_t gauge_show_attrs(struct device *dev, struct device_attribute *attr, char *buf)
{
    const ptrdiff_t offset = attr - gauge_attrs;
    int i = 0;

    switch (offset) {
    case HRADINFO_MAIN_BUCK_INFO:
        if (HARDINFO_ERROR != _hwinfo_data.main_chg) {
            i += scnprintf(buf + i, PAGE_SIZE - i, "%s\n",
                hardinfo_vendor_chg_text[_hwinfo_data.main_chg]);
        } else {
            i += scnprintf(buf + i, PAGE_SIZE - i, "%s\n","Invalid\n");
        }
        break;
    case HRADINFO_BUCK_CHG_INFO:
        if (HARDINFO_ERROR != _hwinfo_data.buck_chg) {
            i += scnprintf(buf + i, PAGE_SIZE - i, "%s\n",
                hardinfo_vendor_chg_text[_hwinfo_data.buck_chg]);
        } else {
            i += scnprintf(buf + i, PAGE_SIZE - i, "%s\n","Invalid\n");
        }
        break;
    case HRADINFO_CHG_PUMP_INFO:
        if (HARDINFO_ERROR != _hwinfo_data.chg_pump) {
            i += scnprintf(buf + i, PAGE_SIZE - i, "%s\n",
                hardinfo_vendor_chg_text[_hwinfo_data.chg_pump]);
        } else {
            i += scnprintf(buf + i, PAGE_SIZE - i, "%s\n","Invalid\n");
        }
        break;
    case HRADINFO_GAUGE_INFO:
        if (HARDINFO_ERROR != _hwinfo_data.gauge) {
            i += scnprintf(buf + i, PAGE_SIZE - i, "%s\n",
                hardinfo_vendor_chg_text[_hwinfo_data.gauge]);
        } else {
            i += scnprintf(buf + i, PAGE_SIZE - i, "%s\n","Invalid\n");
        }
        break;
    case HRADINFO_REAL_TYPE:
        _hwinfo_data.chg_type = mtk_chr_get_type();
        pr_info("%s %d\n", __func__, _hwinfo_data.chg_type);
        if (HARDINFO_ERROR != _hwinfo_data.chg_type) {
            i += scnprintf(buf + i, PAGE_SIZE - i, "%s\n",
                hardinfo_chg_type_text[_hwinfo_data.chg_type]);
        } else {
            i += scnprintf(buf + i, PAGE_SIZE - i, "%s\n","Invalid\n");
        }
        break;
/* Spruce code for OSPURCET-344 by zhangjb18 at 2022.12.07 start */
    case HRADINFO_HIZ_EN:
        i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", _hwinfo_data.hiz_en);
        break;
/* Spruce code for OSPURCET-344 by zhangjb18 at 2022.12.07 end */
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 start */
    case HRADINFO_CHARGING_ENABLED:
	i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", _hwinfo_data.charging);
        break;
    case HRADINFO_VBUS_NOW:
        i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", hardinfo_get_vbus());
        break;
    case HRADINFO_IBUS_NOW:
        i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", hardinfo_get_ibus());
        break;
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 end */
/* Spruce code for OSPURCET-33 by zhangjb18 at 2022.12.12 start */
    case BATTERY_MAINTENANCE_ENABLE:
        i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", _hwinfo_data.bm_flag);
        break;
    case BATTERY_PROTECTION_ENABLE:
        i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", _hwinfo_data.bp_flag);
        break;
    case BATTERY_MAINTENANCE_FAKE_CYCLE:
        i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", _hwinfo_data.fake_cycle);
        break;
    case BATTERY_FAKE_TEMP:
        i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", _hwinfo_data.fake_temp);
        break;
/* Spruce code for OSPURCET-33 by zhangjb18 at 2022.12.12 end */
    /* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 start */
    case BATTERY_THERMAL_CONTROL_LIMIT:
        i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", _hwinfo_data.thermal_limit_level);
        break;
    case BATTERY_THERMAL_CONTROL_LIMIT_MAX:
        i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", _hwinfo_data.thermal_levels);
        break;
    /* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 end */
    /* Spruce code for OSPURCET-463 by zhangjb18 at 2022.12.28 start */
    case HRADINFO_MAIN_CHARGING_CURRENT:
    case HRADINFO_SLAVE_CHARGING_CURRENT:
    case HRADINFO_CHGPUMP_CHARGING_CURRENT:
        i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", hardinfo_get_charging_current(offset));
        break;
    /* Spruce code for OSPURCET-463 by zhangjb18 at 2022.12.28 end */
    default:
        return -EINVAL;
        break;
    }
    return i;
}

static int hardinfo_gauge_create_attrs(struct device *dev)
{
    unsigned long i;
    int rc;

    for (i = 0; i < ARRAY_SIZE(gauge_attrs); i++) {
        rc = device_create_file(dev, &gauge_attrs[i]);
        if (rc)
            goto create_attrs_failed;
    }
    return rc;

create_attrs_failed:
    pr_err("%s: failed (%d)\n", __func__, rc);
    while (i--)
        device_remove_file(dev, &gauge_attrs[i]);
    return rc;
}

/* Spruce code for OSPURCET-33 by zhangjb18 at 2022.12.12 start */
static u32 get_bm_cv(struct hardinfo_charger *hinfo)
{
    int fake_cc = -1;
    int cc = -1;
    union power_supply_propval value;
    int aging_index = HW_LEVEL_0;
    int bm_flag, ret = 0;

    bm_flag = hardinfo_get_bm();
    if (bm_flag) {
        fake_cc = _hwinfo_data.fake_cycle;
    }

    ret = power_supply_get_property(hinfo->gpsy, POWER_SUPPLY_PROP_CYCLE_COUNT, &value);
    if(!ret) {
        cc = value.intval;
    }

    if (bm_flag && cc != -1) {
        if((fake_cc >= 1050) || (cc >= 1050)) {
            aging_index = HW_LEVEL_3;
        } else if((1020 <= fake_cc && fake_cc < 1050) || (1020 <= cc && cc < 1050)) {
            aging_index = HW_LEVEL_2;
        } else if((150 <= fake_cc && fake_cc < 1020) || (150 <= cc && cc < 1020)) {
            aging_index = HW_LEVEL_1;
        } else {
            aging_index = HW_LEVEL_0;
        }
    }

    value.intval = hw_aging_table[aging_index].bm_cv/1000;
    ret = power_supply_set_property(hinfo->gpsy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &value);
    if(ret) {
        pr_notice("[%s]set bm cv fail\n", __func__);
    }

    pr_notice("[%s][bm][bp] bm_flag:%d, bp_flag:%d, fake_cc:%d, cc:%d, bm_cv:%d, chg_cv:%d,\n",
            __func__, bm_flag, hardinfo_get_bp(), fake_cc, cc,
            hw_aging_table[aging_index].bm_cv,
            hw_aging_table[aging_index].chg_cv);

    return hw_aging_table[aging_index].chg_cv;
}

int bm_updata_func(void)
{
    struct hardinfo_charger *hinfo = _hwinfo;
    if(IS_ERR_OR_NULL(hinfo) || IS_ERR_OR_NULL(hinfo->gpsy)) {
        return -ENODEV;
    }

    return get_bm_cv(hinfo);
}

int bp_updata_func(void)
{
    int ret = 0, capcaity;
    union power_supply_propval val;
    int bp_flag = hardinfo_get_bp();
    static int lt_bp_flag = 0;
    struct hardinfo_charger *hinfo = _hwinfo;
    if(IS_ERR_OR_NULL(hinfo) || IS_ERR_OR_NULL(hinfo->gpsy)) {
        return -ENODEV;
    }

    /* Spruce code for OSPURCET-1932 by xuyc12 at 2023.03.07 start */
    if (bp_flag == true) {
        ret = power_supply_get_property(hinfo->gpsy, POWER_SUPPLY_PROP_CAPACITY, &val);
        if (ret) {
            pr_notice("[%s][bp] get capcity fail\n", __func__);
            return -ENODEV;
        }

        capcaity = val.intval;
        if (capcaity > 60) {
            bq_input_stop = true;
            bq_charger_stop = false;
            bp_level = CAPCAITY_ABOVE_60;
        } else if (capcaity == 60) {
            bq_input_stop = false;
            bq_charger_stop = true;
            bp_level = CAPCAITY_ABOVE_60;
        } else if (capcaity < 40) {
            bq_input_stop = false;
            bq_charger_stop = false;
            bp_level = CAPCAITY_BELOW_40;
        } else if (capcaity >= 40 && capcaity < 60 &&
                    (bp_level == CAPCAITY_BELOW_40 ||
                    bp_level == CAPCAITY_ABOVE_60)) {
            bq_input_stop = false;
            bq_charger_stop = false;
        } else {
            bq_input_stop = false;
            bq_charger_stop = true;
            bp_level = CAPCAITY_40_TO_60;
        }
        pr_debug("[%s][bp] capcaity = %d, bp_level = %d\n", __func__, capcaity, bp_level);
    } else if((lt_bp_flag != bp_flag) && !bp_flag) {
        bq_input_stop = false;
        bq_charger_stop = false;
        bp_level = BP_UNWORK;
    }
    /* Spruce code for OSPURCET-1932 by xuyc12 at 2023.03.07 end */

    lt_bp_flag = bp_flag;

    return 0;
}
/* Spruce code for OSPURCET-33 by zhangjb18 at 2022.12.12 end */

/* Spruce code for OSPURCET-39 by liugong2 at 20221207 start */
static void check_usb_thermal(struct work_struct *work)
{
    int ret = 0;
    int usb_temp = 0;
    int bat_temp = 0;
    union power_supply_propval prop;
	struct delayed_work *dwork = to_delayed_work(work);
	struct hardinfo_charger *hinfo = container_of(dwork, struct hardinfo_charger, check_usb_thermal_work);

    ret = get_usb_ntc_temp(hinfo, &usb_temp);
    if(ret < 0){
        dev_err(hinfo->dev, "can not get usb temp");
        goto exit;
    }
    ret = power_supply_get_property(hinfo->gpsy, POWER_SUPPLY_PROP_TEMP, &prop);
    if(ret){
        dev_err(hinfo->dev, "can not get battery temp by psy");
        goto exit;
    }
    bat_temp = prop.intval / 10;

    dev_info(hinfo->dev, "usb_temp = %d, batt_temp = %d usb_mos_gpio = %d", usb_temp, bat_temp, gpio_get_value(hinfo->usb_mos_gpio));

    if(usb_temp > 50 && (usb_temp - bat_temp > 20) ) {
        gpio_set_value(hinfo->usb_mos_gpio, 1);
    } else if((usb_temp < 40) && (usb_temp - bat_temp > 10)){
        gpio_set_value(hinfo->usb_mos_gpio, 0);
    }

exit:
    schedule_delayed_work(&hinfo->check_usb_thermal_work, msecs_to_jiffies(USB_THERMAL_INTERVAL));
}

static int hardinfo_parse_dt(struct hardinfo_charger *hinfo, struct device *dev)
{
    int ret = 0, byte_len;
    struct device_node *nd = dev->of_node;

    hinfo->usb_mos_gpio = of_get_named_gpio(nd, "usb-mos-crtl-gpio", 0);
    if(!gpio_is_valid(hinfo->usb_mos_gpio)){
        dev_err(hinfo->dev, "get gpio failed!!!");
        return -EINVAL;
    }

    ret = gpio_request(hinfo->usb_mos_gpio, "usb protection against fire pin");
    if(ret){
        dev_err(hinfo->dev, "usb_mos_gpio request failed!!!");
        return ret;
    }

    gpio_direction_output(hinfo->usb_mos_gpio, 0);

    /* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 start */
    if (of_find_property(nd, "battery,thermal-mitigation-20w", &byte_len)) {

        hinfo->thermal_mitigation_20w = devm_kzalloc(hinfo->dev, byte_len,
            GFP_KERNEL);

        if (hinfo->thermal_mitigation_20w == NULL)
            return -ENOMEM;

        _hwinfo_data.thermal_levels = byte_len / sizeof(u32);
        ret = of_property_read_u32_array(nd,
            "battery,thermal-mitigation-20w",
            hinfo->thermal_mitigation_20w,
            _hwinfo_data.thermal_levels);
        if (ret < 0) {
            dev_err(hinfo->dev,
                "Couldn't read 20w threm limits rc = %d\n", ret);
            return ret;
        }
    }

    if (of_find_property(nd, "battery,thermal-mitigation-30w", &byte_len)) {
        hinfo->thermal_mitigation_30w = devm_kzalloc(hinfo->dev, byte_len,
            GFP_KERNEL);

        if (hinfo->thermal_mitigation_30w == NULL)
            return -ENOMEM;

        _hwinfo_data.thermal_levels = byte_len / sizeof(u32);
        ret = of_property_read_u32_array(nd,
            "battery,thermal-mitigation-30w",
            hinfo->thermal_mitigation_30w,
            _hwinfo_data.thermal_levels);
        if (ret < 0) {
            dev_err(hinfo->dev,
                "Couldn't read 30w threm limits rc = %d\n", ret);
            return ret;
        }
    }
    /* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 end */

    return ret;
}
/* Spruce code for OSPURCET-39 by liugong2 at 20221207 end*/

static int hardinfo_charger_probe(struct platform_device *pdev)
{
    int ret;
    struct hardinfo_charger *hinfo = devm_kzalloc(&pdev->dev, sizeof(*hinfo), GFP_KERNEL);
    if (!hinfo)
        return -ENOMEM;

    /* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 start */
    hinfo->dev = &pdev->dev;
    _hwinfo = hinfo;
    hinfo->hwinfo_data = &_hwinfo_data;
    /* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 end */

    /* Spruce code for OSPURCET-39 by liugong2 at 20221207 start */
    ret = hardinfo_parse_dt(hinfo, &pdev->dev);
    if(ret){
        dev_err(&pdev->dev, "hardinfo parse dt failed!!!");
        return -EFAULT;
    }



    INIT_DELAYED_WORK(&hinfo->check_usb_thermal_work, check_usb_thermal);

    /* usb burning ntc */
    hinfo->usb_burning_adc = iio_channel_get(&pdev->dev, "usb_burning_ntc");
	if (IS_ERR(hinfo->usb_burning_adc)) {
		ret = PTR_ERR(hinfo->usb_burning_adc);
        dev_err(&pdev->dev, "usb ntc iio channel get failed!!!");
		return ret;
	}
    /* Spruce code for OSPURCET-39 by liugong2 at 20221207 end */

    hinfo->gpsy = power_supply_get_by_name("battery");
    if(IS_ERR_OR_NULL(hinfo->gpsy)) {
        return -EPROBE_DEFER;
    }

    ret = hardinfo_gauge_create_attrs(hinfo->dev);
    if (ret) {
        pr_err("failed to register platform: %d\n",ret);
        return ret;
    }

    ret = hardinfo_gauge_create_attrs(&hinfo->gpsy->dev);
    if (ret) {
        pr_err("failed to register battery: %d\n",ret);
        return ret;
    }

    /* Spruce code for OSPURCET-39 by liugong2 at 20221207 start */
    platform_set_drvdata(pdev, hinfo);
    schedule_delayed_work(&hinfo->check_usb_thermal_work, msecs_to_jiffies(USB_THERMAL_INTERVAL));
    /* Spruce code for OSPURCET-39 by liugong2 at 20221207 end */

    return 0;
}

static int hardinfo_charger_remove(struct platform_device *pdev)
{
    /* Spruce code for OSPURCET-39 by liugong2 at 20221207 start */
    struct hardinfo_charger *hinfo = platform_get_drvdata(pdev);
    iio_channel_release(hinfo->usb_burning_adc);
    cancel_delayed_work(&hinfo->check_usb_thermal_work);
    gpio_free(hinfo->usb_mos_gpio);
    /* Spruce code for OSPURCET-39 by liugong2 at 20221207 end */
    return 0;
}

static const struct of_device_id hardinfo_charger_of_match[] = {
    { .compatible = "huaqin,hardinfo_charger" },
    { }
};

static struct platform_driver hardinfo_charger_driver = {
    .driver = {
        .name = "hardinfo_charger",
        .of_match_table = of_match_ptr(hardinfo_charger_of_match),
    },
    .probe = hardinfo_charger_probe,
    .remove = hardinfo_charger_remove,
};

static int __init hardinfo_charger_init(void)
{
    return platform_driver_register(&hardinfo_charger_driver);
}

static void __exit hardinfo_charger_exit(void)
{
    platform_driver_unregister(&hardinfo_charger_driver);
}

module_init(hardinfo_charger_init);
module_exit(hardinfo_charger_exit);

MODULE_AUTHOR("hardware infomation driver");
MODULE_DESCRIPTION("hardware infomation driver");
MODULE_LICENSE("GPL");
