MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/include
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/include/mt-plat
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/pmic/include

obj-$(CONFIG_SGM415XX_BUCK_CHARGE)	+= sgm415xx_buck_charge.o
obj-$(CONFIG_SC8960X_BUCK_CHARGE)	+= sc8960x_buck_charge.o
obj-$(CONFIG_PD_POLICY_MANAGER)	+= pd_policy_manager.o
obj-y += sgm41600_charge_pump.o sc854x_charger_pump.o charge_pump_manage.o
