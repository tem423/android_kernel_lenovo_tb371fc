# SPDX-License-Identifier: GPL-2.0
#

<<<<<<< HEAD
=======
ifneq ($(DTB),)
>>>>>>> origin/4.19.325
obj-y += linked_dtb.o

# Ensure system.dtb exists
$(obj)/linked_dtb.o: $(obj)/system.dtb

# Generate system.dtb from $(DTB).dtb
ifneq ($(DTB),system)
$(obj)/system.dtb: $(obj)/$(DTB).dtb
	$(call if_changed,cp)
endif
<<<<<<< HEAD
=======
endif
>>>>>>> origin/4.19.325

quiet_cmd_cp = CP      $< $@$2
	cmd_cp = cat $< >$@$2 || (rm -f $@ && echo false)

# Rule to build device tree blobs
DTC_FLAGS := -p 1024
