# SPDX-License-Identifier: GPL-2.0-only

obj-$(CONFIG_MM8013C06_BATTERY) += mm8013c06_battery.o

obj-$(CONFIG_QPNP_SMB5) += qpnp-smb5-main.o
qpnp-smb5-main-y += step-chg-jeita.o battery.o qpnp-smb5.o smb5-lib.o pmic-voter.o storm-watch.o schgm-flash.o battery-profile-loader.o smb5-iio.o lenovo-jeita.o

obj-$(CONFIG_SMB1360_CHARGER_FG) += smb1360-charger-fg.o

obj-$(CONFIG_SMB1398_CHARGER) += qcom-smb1398-charger.o smb1398-charger.o pmic-voter.o
qcom-smb1398-charger-y += smb1398-charger.o pmic-voter.o

obj-$(CONFIG_QPNP_SMBLITE) += qpnp-smblite-main.o
qpnp-smblite-main-y += step-chg-jeita.o battery.o qpnp-smblite.o smblite-lib.o pmic-voter.o storm-watch.o battery-profile-loader.o schgm-flashlite.o smblite-iio.o

obj-$(CONFIG_SMB1355_SLAVE_CHARGER) += qcom-smb1355-charger.o smb1355-charger.o pmic-voter.o
qcom-smb1355-charger-y += smb1355-charger.o pmic-voter.o

obj-$(CONFIG_SMB1351_USB_CHARGER) += smb1351-charger.o pmic-voter.o battery.o

obj-$(CONFIG_SMB1390_CHARGE_PUMP_PSY) += qcom-smb1390-charger.o smb1390-charger-psy.o pmic-voter.o
qcom-smb1390-charger-y += smb1390-charger-psy.o pmic-voter.o

obj-$(CONFIG_QTI_QBG) += qti-qbg-main.o
qti-qbg-main-y += qti-qbg.o qbg-sdam.o qbg-battery-profile.o battery-profile-loader.o

obj-$(CONFIG_QPNP_QG) += qcom-qpnp-qg.o qpnp-qg.o pmic-voter.o qg-util.o qg-soc.o qg-sdam.o qg-battery-profile.o qg-profile-lib.o fg-alg.o
qcom-qpnp-qg-y += qpnp-qg.o battery-profile-loader.o pmic-voter.o qg-util.o qg-soc.o qg-sdam.o qg-battery-profile.o qg-profile-lib.o fg-alg.o

obj-$(CONFIG_QPNP_SMB2) += step-chg-jeita.o battery.o qpnp-smb2.o smb-lib.o pmic-voter.o storm-watch.o

obj-$(CONFIG_SMB138X_CHARGER) += step-chg-jeita.o smb138x-charger.o smb-lib.o pmic-voter.o storm-watch.o battery.o

obj-$(CONFIG_QPNP_FG_GEN3) += qpnp-fg-gen3.o fg-memif.o fg-util.o

obj-$(CONFIG_QPNP_QNOVO5) += qpnp-qnovo5.o battery.o pmic-voter.o

obj-$(CONFIG_QPNP_FG_GEN4) += qpnp-fg-gen4.o fg-memif.o fg-util.o fg-alg.o pmic-voter.o

obj-$(CONFIG_HL6111R) += hl6111r.o

obj-$(CONFIG_QPNP_VM_BMS) += qpnp-vm-bms.o batterydata-lib.o batterydata-interface.o

obj-$(CONFIG_QPNP_LINEAR_CHARGER) += qpnp-linear-charger.o
