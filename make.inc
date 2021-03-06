##
## kernel/linux/make.inc
##
## History:
##    2012/06/01 - [Cao Rongrong] Created file
##
## Copyright (C) 2012-2016, Ambarella, Inc.
##
## All rights reserved. No Part of this file may be reproduced, stored
## in a retrieval system, or transmitted, in any form, or by any means,
## electronic, mechanical, photocopying, recording, or otherwise,
## without the prior consent of Ambarella, Inc.
##

ARCH := arm
export ARCH

LOCAL_PATH:=$(call my-dir)

#####

.PHONY: linux

linux: $(DOT_CONFIG)
	@mkdir -p $(LINUX_OUT_DIR)
	@if [ -f $(LINUX_OUT_DIR)/.config ]; then \
		echo "Build Linux with previous configuration ..."; \
	else \
		$(MAKE) $(AMBA_MAKE_PARA) defconfig_public_linux; \
	fi
	$(AMBA_MAKEFILE_V)$(MAKE) $(AMBA_MAKE_PARA) -C $(LINUX_SRC_DIR) O=$(LINUX_OUT_DIR) all
	@if [ -n "$(KERNEL_INSTALL_PATH)" ]; then \
		echo "Install Linux modules to $(KERNEL_INSTALL_PATH) ..."; \
		mkdir -p $(KERNEL_INSTALL_PATH); \
		$(MAKE) $(AMBA_MAKE_PARA) -C $(LINUX_SRC_DIR) O=$(LINUX_OUT_DIR) \
			$(LINUX_INSTALL_FLAG) modules_install; \
		find $(KERNEL_INSTALL_PATH)/lib/modules/ -name build | xargs -l1 rm -rf; \
		find $(KERNEL_INSTALL_PATH)/lib/modules/ -name source | xargs -l1 rm -rf; \
	fi
	@cp -dpRf $(LINUX_OUT_DIR)/arch/arm/boot/Image $(KERNEL_OUT_DIR)/Image
	@echo "Build $@ Done."

$(call add-target-into-build, linux)

###

.PHONY: menuconfig_public_linux

menuconfig_public_linux: $(DOT_CONFIG)
	@mkdir -p $(LINUX_OUT_DIR)
	@$(MAKE) $(AMBA_MAKE_PARA) -C $(LINUX_SRC_DIR) O=$(LINUX_OUT_DIR) menuconfig

###

.PHONY: defconfig_public_linux

defconfig_public_linux: $(DOT_CONFIG)
	@echo "Build Linux with $(KERNEL_DEFCONFIG) ..."
	@mkdir -p $(LINUX_OUT_DIR)
	@if [ -f $(WORK_DIR)/config/kernel/$(KERNEL_DEFCONFIG) ]; then \
		cp -dpRf $(WORK_DIR)/config/kernel/$(KERNEL_DEFCONFIG) $(LINUX_OUT_DIR)/.config; \
	else \
		$(MAKE) $(AMBA_MAKE_PARA) -C $(LINUX_SRC_DIR) O=$(LINUX_OUT_DIR) $(KERNEL_DEFCONFIG); \
	fi


#####

ifeq ($(CONFIG_KERNEL_DUAL_CPU),y)

KERNEL_2NDCONFIG	:= $(shell echo $(CONFIG_KERNEL_2NDCONFIG_STRING))
LINUX_OUT_DIR_2		:= $(KERNEL_OUT_DIR)/linux_$(strip \
				$(shell echo $(KERNEL_2NDCONFIG) | \
				sed -e s/ambarella_// -e s/_defconfig//))

.PHONY: linux_2

linux_2: $(DOT_CONFIG)
	@mkdir -p $(LINUX_OUT_DIR_2)
	@if [ -f $(LINUX_OUT_DIR_2)/.config ]; then \
		echo "Build Linux_2 with previous configuration ..."; \
	else \
		$(MAKE) $(AMBA_MAKE_PARA) defconfig_public_linux_2; \
	fi
	$(AMBA_MAKEFILE_V)$(MAKE) $(AMBA_MAKE_PARA) -C $(LINUX_SRC_DIR) O=$(LINUX_OUT_DIR_2) all
	@cp -dpRf $(LINUX_OUT_DIR_2)/arch/arm/boot/Image $(KERNEL_OUT_DIR)/Image_2
	@echo "Build $@ Done."

$(call add-target-into-build, linux_2)

###

.PHONY: menuconfig_public_linux_2

menuconfig_public_linux_2: $(DOT_CONFIG)
	@mkdir -p $(LINUX_OUT_DIR_2)
	@$(MAKE) $(AMBA_MAKE_PARA) -C $(LINUX_SRC_DIR) O=$(LINUX_OUT_DIR_2) menuconfig

###

.PHONY: defconfig_public_linux_2

defconfig_public_linux_2: $(DOT_CONFIG)
	@echo "Build Linux_2 with $(KERNEL_2NDCONFIG) ..."
	@mkdir -p $(LINUX_OUT_DIR_2)
	@if [ -f $(WORK_DIR)/config/kernel/$(KERNEL_2NDCONFIG) ]; then \
		cp -dpRf $(WORK_DIR)/config/kernel/$(KERNEL_2NDCONFIG) $(LINUX_OUT_DIR_2)/.config; \
	else \
		$(MAKE) $(AMBA_MAKE_PARA) -C $(LINUX_SRC_DIR) O=$(LINUX_OUT_DIR_2) $(KERNEL_2NDCONFIG); \
	fi

endif

