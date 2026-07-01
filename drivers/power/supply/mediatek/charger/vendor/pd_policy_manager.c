// SPDX-License-Identifier: GPL-2.0
/*
* Copyright (c) 2022 Southchip Semiconductor Technology(Shanghai) Co., Ltd.
*/
#define pr_fmt(fmt)	"[SC-USBPD-PM]: %s: " fmt, __func__

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/power_supply.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
//#include <linux/usb/usbpd.h>
#include "pd_policy_manager.h"
#include <v1/charger_class.h>
#include <v1/mtk_charger.h>
#include <../mtk_charger_intf.h>

//config battery charge full voltage
/* Spruce code for OSPURCET-344 by zhangjb18 at 2022.12.27 start */
#define BATT_MAX_CHG_VOLT           4450
/* Spruce code for OSPURCET-344 by zhangjb18 at 2022.12.27 end */

//config fast charge current
#define BATT_FAST_CHG_CURR          6000

//config vbus max voltage
#define	BUS_OVP_THRESHOLD           11000

//config open CP vbus/vbat
#define BUS_VOLT_INIT_UP            215 / 100
#define BUS_VOLT_MIN                207 / 100
#define BUS_VOLT_MAX                220 / 100

//config monitor time (ms)
/* Spruce code for OSPURCET-1426 by wumz4 at 2023.03.07 start */
#define PM_WORK_RUN_INTERVAL        50
/* Spruce code for OSPURCET-1426 by wumz4 at 2023.03.07 end */

#define BAT_VOLT_LOOP_LMT           BATT_MAX_CHG_VOLT
#define BAT_CURR_LOOP_LMT           BATT_FAST_CHG_CURR
#define BUS_VOLT_LOOP_LMT           BUS_OVP_THRESHOLD

enum {
    PM_ALGO_RET_OK,
    PM_ALGO_RET_CHG_DISABLED,
    PM_ALGO_RET_TAPER_DONE,
};

static struct pdpm_config pm_config = {
    .bat_volt_lp_lmt            = BAT_VOLT_LOOP_LMT,
    .bat_curr_lp_lmt            = BAT_CURR_LOOP_LMT,
    .bus_volt_lp_lmt            = BUS_VOLT_LOOP_LMT,
    .bus_curr_lp_lmt            = (BAT_CURR_LOOP_LMT >> 1),

    //config CP to main charger current(ma)
    /* Spruce code for OSPURCET-344 by zhangjb18 at 2022.12.27 start */
    .fc2_taper_current          = 3000,
    /* Spruce code for OSPURCET-344 by zhangjb18 at 2022.12.27 end */
    //config adapter voltage step(PPS:1-->20mV)
    .fc2_steps                  = 1,
    //config adapter pps pdo min voltage
    .min_adapter_volt_required  = 11000,
    //config adapter pps pdo min curremt
    .min_adapter_curr_required  = 2000,
    //config CP charge min vbat voltage
    .min_vbat_for_cp            = 3500,
    //config standalone(false) or master+slave(true) CP
    .cp_sec_enable              = false,
    //config CP charging, main charger is disable
    .fc2_disable_sw			    = true,
};

static struct usbpd_pm *__pdpm;
static int fcc_curr;
/* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 start */
static int usbpd_pm_enable_cp(struct usbpd_pm *pdpm, bool enable);
static int usbpd_select_pdo(struct usbpd_pm *pdpm, u32 mV, u32 mA);
/* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 end */

/****************Charge Pump API*****************/
static void usbpd_check_cp_psy(struct usbpd_pm *pdpm)
{
    int id;

    if(!pdpm->cp_psy) {
       id = hardinfo_get_vendor_chginfo(HRADINFO_CHG_PUMP_INFO);
       switch (id)
       {
       case SC8549:
            if (pm_config.cp_sec_enable)
                pdpm->cp_psy = power_supply_get_by_name("sc854x-master");
            else{
                pdpm->cp_psy = power_supply_get_by_name("sc854x-standalone");
            }
            break;
       case SGM41600:
            if (pm_config.cp_sec_enable)
                pdpm->cp_psy = power_supply_get_by_name("sgm41600-master");
            else{
                pdpm->cp_psy = power_supply_get_by_name("sgm41600-standalone");
            }
            break;
       default:
        break;
       }
    }

    if (!pdpm->cp_psy)
        pr_err("%s cp_psy not found\n", __func__);
}

static void usbpd_check_cp_sec_psy(struct usbpd_pm *pdpm)
{
    if (!pdpm->cp_sec_psy) {
        pdpm->cp_sec_psy = power_supply_get_by_name("sc854x-slave");
        if (!pdpm->cp_sec_psy)
            pr_err("cp_sec_psy not found\n");
    }
}

static void usbpd_check_err_bus(struct usbpd_pm *pdpm, union power_supply_propval val)
{
    int id;

    id = hardinfo_get_vendor_chginfo(HRADINFO_CHG_PUMP_INFO);
    switch (id)
    {
    case SC8549:
        pdpm->cp.vbus_err_low = !!(val.intval & 0x20);
        pdpm->cp.vbus_err_high = !!(val.intval & 0x10);
        break;
    case SGM41600:
        pdpm->cp.vbus_err_low = !!(val.intval & 0x02);
        pdpm->cp.vbus_err_high = !!(val.intval & 0x01);
        break;
    default:
        pr_err("%s not found\n", __func__);
        break;
    }
}

int usbpd_pm_get_cp_status(void)
{
    if(__pdpm)
        return __pdpm->cp.charge_enabled;

    return 0;
}

/* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 start */
int usbpd_pm_set_cp_status(bool enable)
{
    if(__pdpm && __pdpm->tcpc) {
        usbpd_select_pdo(__pdpm, 5000, 3000);
        usbpd_pm_enable_cp(__pdpm, enable);
        __pdpm->state = PD_PM_STATE_ENTRY;
    }

    return 0;
}
/* Spruce code for OSPURCET-605 by zhangjb18 at 2022.12.16 end */

static void usbpd_pm_update_cp_status(struct usbpd_pm *pdpm)
{
    int ret;
    union power_supply_propval val = {0,};

    usbpd_check_cp_psy(pdpm);

    if (!pdpm->cp_psy)
        return;

    ret = power_supply_get_property(pdpm->cp_psy,
            POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
    if (!ret)
        pdpm->cp.vbus_volt = val.intval; 

    ret = power_supply_get_property(pdpm->cp_psy,
            POWER_SUPPLY_PROP_CURRENT_NOW, &val);
    if (!ret)
        pdpm->cp.ibus_curr = val.intval;
    
    ret = power_supply_get_property(pdpm->cp_psy,
            POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &val);
    if (!ret)
        pdpm->cp.vbat_volt = val.intval;

    ret = power_supply_get_property(pdpm->cp_psy,
            POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT, &val);
    if (!ret)
        pdpm->cp.ibat_volt = val.intval;

    ret = power_supply_get_property(pdpm->cp_psy,
            POWER_SUPPLY_PROP_CHARGE_COUNTER, &val);
    if (!ret) {
        usbpd_check_err_bus(pdpm, val);
    }

    ret = power_supply_get_property(pdpm->cp_psy,
            POWER_SUPPLY_PROP_TEMP, &val);
    if (!ret)
        pdpm->cp.die_temp = val.intval; 

    ret = power_supply_get_property(pdpm->cp_psy,
            POWER_SUPPLY_PROP_ONLINE, &val);
    if (!ret)
        pdpm->cp.charge_enabled = val.intval;
}

static void usbpd_pm_update_cp_sec_status(struct usbpd_pm *pdpm)
{
    int ret;
    union power_supply_propval val = {0,};

    if (!pm_config.cp_sec_enable)
        return;

    usbpd_check_cp_sec_psy(pdpm);
    
    if (!pdpm->cp_sec_psy)
        return;

    ret = power_supply_get_property(pdpm->cp_sec_psy,
            POWER_SUPPLY_PROP_CURRENT_NOW, &val);
    if (!ret)
        pdpm->cp_sec.ibus_curr = val.intval; 

    ret = power_supply_get_property(pdpm->cp_sec_psy,
            POWER_SUPPLY_PROP_ONLINE, &val);
    if (!ret)
        pdpm->cp_sec.charge_enabled = val.intval;
}

static int usbpd_pm_enable_cp(struct usbpd_pm *pdpm, bool enable)
{
    int ret;
    union power_supply_propval val = {0,};

    usbpd_check_cp_psy(pdpm);

    if (!pdpm->cp_psy)
        return -ENODEV;
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 start */
    if(!hardinfo_get_chg_online() || !hardinfo_get_charging())
        return -EINVAL;
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 end */

    val.intval = enable;
    ret = power_supply_set_property(pdpm->cp_psy,
            POWER_SUPPLY_PROP_ONLINE, &val);

    return ret;
}

static int usbpd_pm_enable_cp_sec(struct usbpd_pm *pdpm, bool enable)
{
    int ret;
    union power_supply_propval val = {0,};

    usbpd_check_cp_sec_psy(pdpm);
    
    if (!pdpm->cp_sec_psy)
        return -ENODEV;

    val.intval = enable;
    ret = power_supply_set_property(pdpm->cp_sec_psy, 
            POWER_SUPPLY_PROP_ONLINE, &val);
    
    return ret;
}

static int usbpd_pm_check_cp_enabled(struct usbpd_pm *pdpm)
{
    int ret;
    union power_supply_propval val = {0,};

    usbpd_check_cp_psy(pdpm);

    if (!pdpm->cp_psy)
        return -ENODEV;

/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 start */
    if(!hardinfo_get_chg_online() || !hardinfo_get_charging()) {
        pdpm->cp.charge_enabled = 0;
        return -EINVAL;
    }
/* Spruce code for OSPURCET-450 by zhangjb18 at 2022.12.05 end */

    ret = power_supply_get_property(pdpm->cp_psy,
            POWER_SUPPLY_PROP_ONLINE, &val);
    if (!ret)
        pdpm->cp.charge_enabled = !!val.intval;

    return ret;
}

static int usbpd_pm_check_cp_sec_enabled(struct usbpd_pm *pdpm)
{
    int ret;
    union power_supply_propval val = {0,};

    usbpd_check_cp_sec_psy(pdpm);

    if (!pdpm->cp_sec_psy) 
        return -ENODEV;

    ret = power_supply_get_property(pdpm->cp_sec_psy, 
            POWER_SUPPLY_PROP_ONLINE, &val);
    if (!ret)
        pdpm->cp_sec.charge_enabled = !!val.intval;
    
    return ret;
}

static int usbpd_pm_enable_sw(struct usbpd_pm *pdpm, bool enable)
{
    /*TODO: config main charger enable or disable
    If the main charger needs to take 100mA current when the CC charging, you 
    can set the max current to 100mA when disable
    */

    /*int ret;
    union power_supply_propval val = {0,};

    if (!pdpm->sw_psy) {
        pdpm->sw_psy = power_supply_get_by_name("battery");
        if (!pdpm->sw_psy) {
            return -ENODEV;
        }
    }

    if(enable)   val.intval = 3000000;
    else         val.intval = 100000;

    ret = power_supply_set_property(pdpm->sw_psy, 
            POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val);

    return ret;*/
    int ua, ret;

    if(!pdpm->chg1_dev && !pdpm->chg2_dev){
        pdpm->chg1_dev = get_charger_by_name("primary_chg");
        if (!pdpm->chg1_dev) {
            pr_info("%s: get primary charger device failed\n", __func__);
            return -ENODEV;
        }
        pdpm->chg2_dev = get_charger_by_name("secondary_chg");
        if (!pdpm->chg2_dev) {
            pr_info("%s: get secondary charger device failed\n", __func__);
            return -ENODEV;
        }
    }

    if(enable)   ua = SW_ENABLE_INPUT_CURRENT;
    else         ua = SW_DISABLE_INPUT_CURRENT;

    ret = charger_dev_set_input_current(pdpm->chg1_dev, ua);

    return ret ;
}

static int usbpd_pm_check_sw_enabled(struct usbpd_pm *pdpm)
{
    /*TODO: check main charger enable or disable
    */
    /*int ret;
    union power_supply_propval val = {0,};

    if (!pdpm->sw_psy) {
        pdpm->sw_psy = power_supply_get_by_name("battery");
        if (!pdpm->sw_psy) {
            return -ENODEV;
        }
    }

    ret = power_supply_get_property(pdpm->sw_psy, 
            POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val);
    if (!ret)
    {
        if(val.intval == 100000)  pdpm->sw.charge_enabled = false;
        else  pdpm->sw.charge_enabled = true;
    }

    return ret;*/
    int ret, ua;

    if(!pdpm->chg1_dev && !pdpm->chg2_dev){
        pdpm->chg1_dev = get_charger_by_name("primary_chg");
        if (!pdpm->chg1_dev) {
            pr_info("%s: get primary charger device failed\n", __func__);
            return -ENODEV;
        }
        pdpm->chg2_dev = get_charger_by_name("secondary_chg");
        if (!pdpm->chg2_dev) {
            pr_info("%s: get secondary charger device failed\n", __func__);
            return -ENODEV;
        } 
    }

    ret = charger_dev_get_input_current(pdpm->chg1_dev, &ua);
    if(!ret) {
        if(ua <= SW_DISABLE_INPUT_CURRENT)
            pdpm->sw.charge_enabled = false;
        else
            pdpm->sw.charge_enabled = true;
    }

    return ret;
}

static void usbpd_pm_update_sw_status(struct usbpd_pm *pdpm)
{
    usbpd_pm_check_sw_enabled(pdpm);
}

/***************PD API****************/
static inline int check_typec_attached_snk(struct tcpc_device *tcpc)
{
    if (tcpm_inquire_typec_attach_state(tcpc) != TYPEC_ATTACHED_SNK)
        return -EINVAL;
    return 0;
}

static int usbpd_pps_enable_charging(struct usbpd_pm *pdpm, bool en,
                u32 mV, u32 mA)
{
    int ret, cnt = 0;

    if (check_typec_attached_snk(pdpm->tcpc) < 0)
        return -EINVAL;
    pr_err("en = %d, %dmV, %dmA\n", en, mV, mA);

    do {
        if (en)
            ret = tcpm_set_apdo_charging_policy(pdpm->tcpc,
                DPM_CHARGING_POLICY_PPS, mV, mA, NULL);
        else
            ret = tcpm_reset_pd_charging_policy(pdpm->tcpc, NULL);
        cnt++;
    } while (ret != TCP_DPM_RET_SUCCESS && cnt < 3);

    if (ret != TCP_DPM_RET_SUCCESS)
        pr_err("fail(%d)\n", ret);
    return ret > 0 ? -ret : ret;
}

static bool usbpd_get_pps_status(struct usbpd_pm *pdpm)
{
    int ret, apdo_idx = -1;
    struct tcpm_power_cap_val apdo_cap = {0};
    u8 cap_idx;

    pr_err("++\n");
    if (check_typec_attached_snk(pdpm->tcpc) < 0)
        return -EINVAL;

    if (!pdpm->is_pps_en_unlock) {
        pr_err("pps en is locked\n");
        return -EINVAL;
    }

    if (!tcpm_inquire_pd_pe_ready(pdpm->tcpc)) {
        pr_err("PD PE not ready\n");
        return -EINVAL;
    }

    /* select TA boundary */
    cap_idx = 0;
    while (1) {
        ret = tcpm_inquire_pd_source_apdo(pdpm->tcpc,
                        TCPM_POWER_CAP_APDO_TYPE_PPS,
                        &cap_idx, &apdo_cap);
        if (ret != TCP_DPM_RET_SUCCESS) {
            pr_err("inquire pd apdo fail(%d)\n", ret);
            break;
        }

        pr_err("cap_idx[%d], %d mv ~ %d mv, %d ma, pl: %d\n", cap_idx,
            apdo_cap.min_mv, apdo_cap.max_mv, apdo_cap.ma,
            apdo_cap.pwr_limit);

        /*
        * !(apdo_cap.min_mv <= data->vcap_min &&
        *   apdo_cap.max_mv >= data->vcap_max &&
        *   apdo_cap.ma >= data->icap_min)
        */
        if (apdo_cap.max_mv < pm_config.min_adapter_volt_required ||
            apdo_cap.ma < pm_config.min_adapter_curr_required)
            continue;
        if (apdo_idx == -1) {
            apdo_idx = cap_idx;
            pdpm->apdo_max_volt = apdo_cap.max_mv;
            pdpm->apdo_max_curr = apdo_cap.ma;
        } else {
            if (apdo_cap.ma > pdpm->apdo_max_curr) {
                apdo_idx = cap_idx;
                pdpm->apdo_max_volt = apdo_cap.max_mv;
                pdpm->apdo_max_curr = apdo_cap.ma;
            }
        }
    }
    if (apdo_idx != -1){
        pr_err("select potential cap_idx[%d]\n", cap_idx);
        ret = usbpd_pps_enable_charging(pdpm, true, 5000, 3000);
        if (ret != TCP_DPM_RET_SUCCESS)
            return false;
        return true;
    }
    return false;
}

static int usbpd_select_pdo(struct usbpd_pm *pdpm, u32 mV, u32 mA)
{
    int ret, cnt = 0;

    if (check_typec_attached_snk(pdpm->tcpc) < 0)
        return -EINVAL;
    pr_err("%dmV, %dmA\n", mV, mA);

    if (!tcpm_inquire_pd_connected(pdpm->tcpc)) {
        pr_err("pd not connected\n");
        return -EINVAL;
    }

    do {
        ret = tcpm_dpm_pd_request(pdpm->tcpc, mV, mA, NULL);
        cnt++;
    } while (ret != TCP_DPM_RET_SUCCESS && cnt < 3);

    if (ret != TCP_DPM_RET_SUCCESS)
        pr_err("fail(%d)\n", ret);
    return ret > 0 ? -ret : ret;
}

static int pca_pps_tcp_notifier_call(struct notifier_block *nb,
                    unsigned long event, void *data)
{
    struct usbpd_pm *pdpm = container_of(nb, struct usbpd_pm, tcp_nb);
    struct tcp_notify *noti = data;

    switch (event) {
    case TCP_NOTIFY_PD_STATE:
        switch (noti->pd_state.connected) {
        case PD_CONNECT_NONE:
            pr_err("detached\n");
            pdpm->is_pps_en_unlock = false;
            pdpm->hrst_cnt = 0;
            break;
        case PD_CONNECT_HARD_RESET:
            pdpm->hrst_cnt++;
            pr_err("pd hardreset, cnt = %d\n",
                pdpm->hrst_cnt);
            pdpm->is_pps_en_unlock = false;
            break;
        case PD_CONNECT_PE_READY_SNK_APDO:
            if (pdpm->hrst_cnt < 5) {
                pr_err("en unlock\n");
                pdpm->is_pps_en_unlock = true;
            }
            break;
        default:
            break;
        }
    default:
        break;
    }

    if (pdpm->usb_psy) {
        power_supply_changed(pdpm->usb_psy);
    }

    return NOTIFY_OK;
}
static void usbpd_check_tcpc(struct usbpd_pm *pdpm)
{
    int ret;

    if (!pdpm->tcpc) {
        pdpm->tcpc = tcpc_dev_get_by_name("type_c_port0");
        if (!pdpm->tcpc) {
            pr_err("get tcpc dev fail\n");
            return;
        }
        pdpm->tcp_nb.notifier_call = pca_pps_tcp_notifier_call;
        ret = register_tcp_dev_notifier(pdpm->tcpc, &pdpm->tcp_nb,
                        TCP_NOTIFY_TYPE_USB);
        if (ret < 0) {
            pr_err("register tcpc notifier fail\n");
        }
    }
}
/*******************main charger API********************/
static void usbpd_check_usb_psy(struct usbpd_pm *pdpm)
{
    if (!pdpm->usb_psy) { 
        pdpm->usb_psy = power_supply_get_by_name("usb");
        if (!pdpm->usb_psy)
            pr_err("usb psy not found!\n");
    }
}

static int usbpd_update_ibat_curr(struct usbpd_pm *pdpm)
{
    /*TODO: update ibat and vbat by gauge
    */
    int ret;
    union power_supply_propval val = {0,};

    if (!pdpm->bms_psy) {
        pdpm->bms_psy = power_supply_get_by_name("battery");
        if (!pdpm->bms_psy) {
            return -ENODEV;
        }
    }

    ret = power_supply_get_property(pdpm->bms_psy, 
            POWER_SUPPLY_PROP_CURRENT_NOW, &val);
    if (!ret)
        pdpm->sw.ibat_curr= (int)(val.intval/1000);

    ret = power_supply_get_property(pdpm->bms_psy, 
            POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
    if (!ret)
        pdpm->sw.vbat_volt = (int)(val.intval/1000);

    return ret;
}


static int usbpd_update_ibus_curr(struct usbpd_pm *pdpm)
{
    /*TODO: update ibus of main charger
    */
    /*int ret;
    union power_supply_propval val = {0,};

    if (!pdpm->sw_psy) {
        pdpm->sw_psy = power_supply_get_by_name("usb");
        if (!pdpm->sw_psy) {
            return -ENODEV;
        }
    }

    ret = power_supply_get_property(pdpm->sw_psy, 
            POWER_SUPPLY_PROP_INPUT_CURRENT_NOW, &val);
    if (!ret)
        pdpm->sw.ibus_curr = (int)(val.intval/1000);

    return ret;*/
    int ua, ret;

    if(!pdpm->chg1_dev && !pdpm->chg2_dev){
        pdpm->chg1_dev = get_charger_by_name("primary_chg");
        if (!pdpm->chg1_dev) {
            pr_info("%s: get primary charger device failed\n", __func__);
            return -ENODEV;
        }
        pdpm->chg2_dev = get_charger_by_name("secondary_chg");
        if (!pdpm->chg2_dev) {
            pr_info("%s: get secondary charger device failed\n", __func__);
            return -ENODEV;
        } 
    }

    ret = charger_dev_get_ibus(pdpm->chg1_dev, &ua);
    if (!ret)
        pdpm->sw.ibus_curr = (int)(ua/1000);

    return 0;
}

static void usbpd_pm_evaluate_src_caps(struct usbpd_pm *pdpm)
{
    bool retValue;

    retValue = usbpd_get_pps_status(pdpm);
    if (retValue)
        pdpm->pps_supported = true;

    
    if (pdpm->pps_supported)
        pr_info("PPS supported, preferred APDO pos:%d, max volt:%d, current:%d\n",
                pdpm->apdo_selected_pdo,
                pdpm->apdo_max_volt,
                pdpm->apdo_max_curr);
    else
        pr_info("Not qualified PPS adapter\n");
}

void usbpd_pm_set_fcc(int fcc)
{
    fcc_curr = fcc/1000;
}

#define TAPER_TIMEOUT	(5000 / PM_WORK_RUN_INTERVAL)
#define IBUS_CHANGE_TIMEOUT  (500 / PM_WORK_RUN_INTERVAL)
static int usbpd_pm_fc2_charge_algo(struct usbpd_pm *pdpm)
{
    int steps;
    int step_vbat = 0;
    int step_ibus = 0;
    int step_ibat = 0;
    int ibus_total = 0;
    int ibat_limit = 0;

    static int ibus_limit;

    if (ibus_limit == 0)
        ibus_limit = pm_config.bus_curr_lp_lmt;

    /* reduce bus current in cv loop */
    if (pdpm->vbat_now > pm_config.bat_volt_lp_lmt - 50) {
        if (pdpm->ibus_lmt_change_timer++ > IBUS_CHANGE_TIMEOUT) {
            pdpm->ibus_lmt_change_timer = 0;
            ibus_limit = pm_config.bus_curr_lp_lmt;
        }
    } else if (pdpm->vbat_now < pm_config.bat_volt_lp_lmt - 250) {
        ibus_limit = pm_config.bus_curr_lp_lmt;
        pdpm->ibus_lmt_change_timer = 0;
    } else {
        pdpm->ibus_lmt_change_timer = 0;
    }

    ibus_limit = min(pdpm->apdo_max_curr, ibus_limit);

    /* battery voltage loop*/
    if (pdpm->vbat_now > pm_config.bat_volt_lp_lmt)
        step_vbat = -pm_config.fc2_steps;
    else if (pdpm->vbat_now < pm_config.bat_volt_lp_lmt - 7)
        step_vbat = pm_config.fc2_steps;


    /* battery charge current loop*/
    /*TODO: thermal contrl ibat_limit = pm_config.bat_curr_lp_lmt;*/ 
    ibat_limit = min(pm_config.bat_curr_lp_lmt, fcc_curr);

    if (pdpm->ibat_now < ibat_limit)
        step_ibat = pm_config.fc2_steps;
    else if (pdpm->ibat_now > ibat_limit + 100)
        step_ibat = -pm_config.fc2_steps;

    /* bus current loop*/
    ibus_total = pdpm->cp.ibus_curr + pdpm->sw.ibus_curr;

    if (pm_config.cp_sec_enable)
        ibus_total += pdpm->cp_sec.ibus_curr;

    if (ibus_total < ibus_limit - 50)
        step_ibus = pm_config.fc2_steps;
    else if (ibus_total > ibus_limit)
        step_ibus = -pm_config.fc2_steps;

    steps = min(min(step_vbat, step_ibus), step_ibat);

    /* check if cp disabled due to other reason*/
    usbpd_pm_check_cp_enabled(pdpm);
    if (pm_config.cp_sec_enable) {
        usbpd_pm_check_cp_sec_enabled(pdpm);
    }

    if (!pdpm->cp.charge_enabled || (pm_config.cp_sec_enable && 
        !pdpm->cp_sec_stopped && !pdpm->cp_sec.charge_enabled)) {
        pr_notice("cp.charge_enabled:%d  %d  %d\n",
                pdpm->cp.charge_enabled, pdpm->cp.vbus_err_low, pdpm->cp.vbus_err_high);
        return PM_ALGO_RET_CHG_DISABLED;
    }

    /* charge pump taper charge */
    if (pdpm->vbat_now > pm_config.bat_volt_lp_lmt - 50
            && pdpm->ibat_now < pm_config.fc2_taper_current) { //zjb  ibat_now --> pdpm->ibat_now 
        if (pdpm->fc2_taper_timer++ > TAPER_TIMEOUT) {
            pr_notice("charge pump taper charging done\n");
            pdpm->fc2_taper_timer = 0;
            return PM_ALGO_RET_TAPER_DONE;
        }
    } else {
        pdpm->fc2_taper_timer = 0;
    }
        
    /*TODO: customer can add hook here to check system level 
        * thermal mitigation*/

    pr_err("%s %d %d %d all %d\n", __func__,  
            step_vbat, step_ibat, step_ibus, steps);

    pdpm->request_voltage += steps * 20;

    if (pdpm->request_voltage > pdpm->apdo_max_volt - 1000)
        pdpm->request_voltage = pdpm->apdo_max_volt - 1000;


    return PM_ALGO_RET_OK;
}

static const unsigned char *pm_str[] = {
    "PD_PM_STATE_ENTRY",
    "PD_PM_STATE_FC2_ENTRY",
    "PD_PM_STATE_FC2_ENTRY_1",
    "PD_PM_STATE_FC2_ENTRY_2",
    "PD_PM_STATE_FC2_ENTRY_3",
    "PD_PM_STATE_FC2_TUNE",
    "PD_PM_STATE_FC2_EXIT",
};

static void usbpd_pm_move_state(struct usbpd_pm *pdpm, enum pm_state state)
{
    pr_err("state change:%s -> %s\n", 
        pm_str[pdpm->state], pm_str[state]);
    pdpm->state = state;
}

static int usbpd_pm_sm(struct usbpd_pm *pdpm)
{
    int ret;
    int rc = 0;
    static int tune_vbus_retry;
    static bool stop_sw;
    static bool recover;

    pr_err("state phase :%d\n", pdpm->state);
    pr_err("vbus_vol %d cp vbat_vol %d sw vbat_vol %d\n", pdpm->cp.vbus_volt, 
            pdpm->cp.vbat_volt, pdpm->sw.vbat_volt);
    pr_err("cp m ibus_curr %d  cp s ibus_curr %d sw ibus_curr %d ibat_curr %d bat_volt_lp_lmt %d\n",
    pdpm->cp.ibus_curr, pdpm->cp_sec.ibus_curr, pdpm->sw.ibus_curr, pdpm->sw.ibat_curr,
    pm_config.bat_volt_lp_lmt);

    pdpm->vbat_now = pdpm->cp.vbat_volt;
    pdpm->ibat_now = pdpm->sw.ibat_curr;
    /* Spruce code for OSPURCET-344 by zhangjb18 at 2022.12.27 start */
    if(!mtk_chr_get_cv())
        pm_config.bat_volt_lp_lmt = mtk_chr_get_cv() - 30000;
    /* Spruce code for OSPURCET-344 by zhangjb18 at 2022.12.27 end */

    switch (pdpm->state) {
    case PD_PM_STATE_ENTRY:
        stop_sw = false;
        recover = false;
        if (pdpm->vbat_now < pm_config.min_vbat_for_cp) {
            pr_notice("batt_volt-%d, waiting...\n", pdpm->vbat_now);
        } else if (pdpm->vbat_now > pm_config.bat_volt_lp_lmt - 100) {
            pr_notice("batt_volt-%d is too high for cp,\
                    charging with switch charger\n", 
                    pdpm->vbat_now);
            usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_EXIT);
        } else {
            pr_notice("batt_volt-%d is ok, start flash charging\n", 
                    pdpm->vbat_now);
            usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_ENTRY);
        }
        break;

    case PD_PM_STATE_FC2_ENTRY:
        if (pm_config.fc2_disable_sw) {
            if (pdpm->sw.charge_enabled) {
                usbpd_pm_enable_sw(pdpm, false);
                usbpd_pm_check_sw_enabled(pdpm);
            }
            if (!pdpm->sw.charge_enabled)
                usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_ENTRY_1);
        } else {
            usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_ENTRY_1);
        }
        break;

    case PD_PM_STATE_FC2_ENTRY_1:
        if (pm_config.cp_sec_enable)
            pdpm->request_voltage = pdpm->vbat_now * BUS_VOLT_INIT_UP + 200;
        else
            pdpm->request_voltage = pdpm->vbat_now * BUS_VOLT_INIT_UP;

        pdpm->request_current = min(pdpm->apdo_max_curr, pm_config.bus_curr_lp_lmt);
        usbpd_select_pdo(pdpm, pdpm->request_voltage, pdpm->request_current);
        pr_err("request_voltage:%d, request_current:%d\n",
                pdpm->request_voltage, pdpm->request_current);

        usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_ENTRY_2);
        tune_vbus_retry = 0;
        break;

    case PD_PM_STATE_FC2_ENTRY_2:
        pr_err("tune_vbus_retry %d\n", tune_vbus_retry);
        if (pdpm->cp.vbus_err_low || (pdpm->cp.vbus_volt < 
                pdpm->vbat_now * BUS_VOLT_MIN)) {
            tune_vbus_retry++;
            pdpm->request_voltage += 20;
            usbpd_select_pdo(pdpm, pdpm->request_voltage, pdpm->request_current);
            pr_err("request_voltage:%d, request_current:%d\n",
                    pdpm->request_voltage, pdpm->request_current);
        } else if (pdpm->cp.vbus_err_high || (pdpm->cp.vbus_volt > 
                pdpm->vbat_now * BUS_VOLT_MAX)) {
            tune_vbus_retry++;
            pdpm->request_voltage -= 20;
            usbpd_select_pdo(pdpm, pdpm->request_voltage, pdpm->request_current);
            pr_err("request_voltage:%d, request_current:%d\n",
                    pdpm->request_voltage, pdpm->request_current);
        } else {
            pr_notice("adapter volt tune ok, retry %d times\n", tune_vbus_retry);
            usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_ENTRY_3);
            break;
        }
        
        if (tune_vbus_retry > 30) {
            pr_notice("Failed to tune adapter volt into valid range, \
                    charge with switching charger\n");
            usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_EXIT);
        }	
        break;
    case PD_PM_STATE_FC2_ENTRY_3:
        usbpd_pm_check_cp_enabled(pdpm);
        if (!pdpm->cp.charge_enabled) {
            usbpd_pm_enable_cp(pdpm, true);
            msleep(100);
            usbpd_pm_check_cp_enabled(pdpm);
        }

        if (pm_config.cp_sec_enable) {
            usbpd_pm_check_cp_sec_enabled(pdpm);
            if(!pdpm->cp_sec.charge_enabled) {
                usbpd_pm_enable_cp_sec(pdpm, true);
                msleep(100);
                usbpd_pm_check_cp_sec_enabled(pdpm);
            }
        }

        if (pdpm->cp.charge_enabled) {
            if ((pm_config.cp_sec_enable && pdpm->cp_sec.charge_enabled)
                    || !pm_config.cp_sec_enable) {
                usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_TUNE);
                pdpm->ibus_lmt_change_timer = 0;
                pdpm->fc2_taper_timer = 0;
            }
        }
        break;

    case PD_PM_STATE_FC2_TUNE:
        ret = usbpd_pm_fc2_charge_algo(pdpm);
        if (ret == PM_ALGO_RET_TAPER_DONE) {
            pr_notice("Move to switch charging:%d\n", ret);
            stop_sw = false;
            pdpm->sw.charge_enabled = false;
            usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_EXIT);
            break;
        } else if (ret == PM_ALGO_RET_CHG_DISABLED) {
            pr_notice("Move to switch charging, will try to recover \
                    flash charging:%d\n", ret);
            recover = true;
            stop_sw = false;
            usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_EXIT);
            break;
        } else {
            usbpd_select_pdo(pdpm, pdpm->request_voltage, pdpm->request_current);
            pr_err("request_voltage:%d, request_current:%d\n",
                    pdpm->request_voltage, pdpm->request_current);
        }

        /*stop second charge pump if either of ibus is lower than 750ma during CV*/
        if (pm_config.cp_sec_enable && pdpm->cp_sec.charge_enabled 
                && pdpm->vbat_now > pm_config.bat_volt_lp_lmt - 50
                && (pdpm->cp.ibus_curr < 750 || pdpm->cp_sec.ibus_curr < 750)) {
            pr_notice("second cp is disabled due to ibus < 750mA\n");
            usbpd_pm_enable_cp_sec(pdpm, false);
            usbpd_pm_check_cp_sec_enabled(pdpm);
            pdpm->cp_sec_stopped = true;
        }
        break;

    case PD_PM_STATE_FC2_EXIT:
        /* select default 9V*/
        usbpd_select_pdo(pdpm, 9000, 3000);

        if (pdpm->cp.charge_enabled) {
            usbpd_pm_enable_cp(pdpm, false);
            usbpd_pm_check_cp_enabled(pdpm);
        }

        if (pm_config.cp_sec_enable && pdpm->cp_sec.charge_enabled) {
            usbpd_pm_enable_cp_sec(pdpm, false);
            usbpd_pm_check_cp_sec_enabled(pdpm);
        }

        pr_err(">>>sw state %d   %d\n", stop_sw, pdpm->sw.charge_enabled);
        if (stop_sw && pdpm->sw.charge_enabled)
            usbpd_pm_enable_sw(pdpm, false);
        else if (!stop_sw && !pdpm->sw.charge_enabled)
            usbpd_pm_enable_sw(pdpm, true);
            
        usbpd_pm_check_sw_enabled(pdpm);

        if (recover)
            usbpd_pm_move_state(pdpm, PD_PM_STATE_ENTRY);
        else
            rc = 1;
        
        break;
    }

    return rc;
}

static void usbpd_pm_workfunc(struct work_struct *work)
{
    struct usbpd_pm *pdpm = container_of(work, struct usbpd_pm, pm_work.work);

    usbpd_pm_update_sw_status(pdpm);
    usbpd_update_ibus_curr(pdpm);
    usbpd_pm_update_cp_status(pdpm);
    usbpd_pm_update_cp_sec_status(pdpm);
    usbpd_update_ibat_curr(pdpm);

    if (!usbpd_pm_sm(pdpm) && pdpm->pd_active)
        schedule_delayed_work(&pdpm->pm_work,
            msecs_to_jiffies(PM_WORK_RUN_INTERVAL));

}

static void usbpd_pm_disconnect(struct usbpd_pm *pdpm)
{
    usbpd_pm_enable_cp(pdpm, false);
    usbpd_pm_check_cp_enabled(pdpm);
    if (pm_config.cp_sec_enable) {
        usbpd_pm_enable_cp_sec(pdpm, false);
        usbpd_pm_check_cp_sec_enabled(pdpm);
    }
    cancel_delayed_work_sync(&pdpm->pm_work);

    if (!pdpm->sw.charge_enabled) {
        usbpd_pm_enable_sw(pdpm, true);
        usbpd_pm_check_sw_enabled(pdpm);
    }

    pdpm->pps_supported = false;
    pdpm->apdo_selected_pdo = 0;

    usbpd_pm_move_state(pdpm, PD_PM_STATE_ENTRY);
}

static void usbpd_pd_contact(struct usbpd_pm *pdpm, bool connected)
{
    pdpm->pd_active = connected;
    pr_err("[SC manager] >> pd_active %d\n", pdpm->pd_active);
    if (connected) {
        msleep(10);
        usbpd_pm_evaluate_src_caps(pdpm);
        pr_err("[SC manager] >>start cp charging pps support %d\n", 
            pdpm->pps_supported);
        if (pdpm->pps_supported)
            schedule_delayed_work(&pdpm->pm_work, 0);
    } else {
        usbpd_pm_disconnect(pdpm);
    }
}

static void cp_psy_change_work(struct work_struct *work)
{
    struct usbpd_pm *pdpm = container_of(work, struct usbpd_pm,
                    cp_psy_change_work);

    pdpm->psy_change_running = false;
}

static void usb_psy_change_work(struct work_struct *work)
{
    // int ret; zjb
    bool pd_pps_online = false;
    struct usbpd_pm *pdpm = container_of(work, struct usbpd_pm,
                    usb_psy_change_work);

    pr_err("[SC manager] >> usb change work\n");


    if (check_typec_attached_snk(pdpm->tcpc) >= 0) {
        if (pdpm->is_pps_en_unlock) {
            pd_pps_online = true;
        }
    }

    pr_err("[SC manager] >> pd_active %d  pd_pps_online %d\n",
            pdpm->pd_active, pd_pps_online);

    if (!pdpm->pd_active && pd_pps_online)
        usbpd_pd_contact(pdpm, true);
    else if (pdpm->pd_active && !pd_pps_online)
        usbpd_pd_contact(pdpm, false);
// out: zjb
    pdpm->psy_change_running = false;
}

static int usbpd_psy_notifier_cb(struct notifier_block *nb, 
            unsigned long event, void *data)
{
    struct usbpd_pm *pdpm = container_of(nb, struct usbpd_pm, nb);
    struct power_supply *psy = data;
    unsigned long flags;
    // union power_supply_propval val = {0,}; // 

    if (event != PSY_EVENT_PROP_CHANGED)
        return NOTIFY_OK;

    usbpd_check_cp_psy(pdpm);
    usbpd_check_usb_psy(pdpm);
    usbpd_check_tcpc(pdpm);

    if (!pdpm->cp_psy || !pdpm->usb_psy)
        return NOTIFY_OK;

    if (psy == pdpm->cp_psy || psy == pdpm->usb_psy) {
        spin_lock_irqsave(&pdpm->psy_change_lock, flags);
        pr_err("[SC manager] >>>pdpm->psy_change_running : %d\n", pdpm->psy_change_running);
        if (!pdpm->psy_change_running) {
            pdpm->psy_change_running = true;
            if (psy == pdpm->cp_psy)
                schedule_work(&pdpm->cp_psy_change_work);
            else
                schedule_work(&pdpm->usb_psy_change_work);
        }
        spin_unlock_irqrestore(&pdpm->psy_change_lock, flags);
    }

    return NOTIFY_OK;
}

static int __init usbpd_pm_init(void)
{
    struct usbpd_pm *pdpm;

    pdpm = kzalloc(sizeof(*pdpm), GFP_KERNEL);
    if (!pdpm)
        return -ENOMEM;

    __pdpm = pdpm;

    INIT_WORK(&pdpm->cp_psy_change_work, cp_psy_change_work);
    INIT_WORK(&pdpm->usb_psy_change_work, usb_psy_change_work);

    spin_lock_init(&pdpm->psy_change_lock);

    usbpd_check_cp_psy(pdpm);
    usbpd_check_cp_sec_psy(pdpm);
    usbpd_check_usb_psy(pdpm);
    usbpd_check_tcpc(pdpm);

    INIT_DELAYED_WORK(&pdpm->pm_work, usbpd_pm_workfunc);

    pdpm->nb.notifier_call = usbpd_psy_notifier_cb;
    power_supply_reg_notifier(&pdpm->nb);

    return 0;
}

static void __exit usbpd_pm_exit(void)
{
    power_supply_unreg_notifier(&__pdpm->nb);
    cancel_delayed_work_sync(&__pdpm->pm_work);
    cancel_work_sync(&__pdpm->cp_psy_change_work);
    cancel_work_sync(&__pdpm->usb_psy_change_work);
}

module_init(usbpd_pm_init);
module_exit(usbpd_pm_exit);

MODULE_DESCRIPTION("SC Charge Pump Policy Manager");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("South Chip <Aiden-yu@southchip.com>");
