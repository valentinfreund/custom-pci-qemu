################################################################################
#
# driverlab-tests
#
################################################################################

DRIVERLAB_TESTS_VERSION = 1.0
DRIVERLAB_TESTS_SITE = $(BR2_EXTERNAL_DRIVERLAB_PATH)/../tests/guest
DRIVERLAB_TESTS_SITE_METHOD = local
DRIVERLAB_TESTS_LICENSE = GPL-2.0

define DRIVERLAB_TESTS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/run-tests $(TARGET_DIR)/usr/bin/driverlab-test
	$(INSTALL) -d $(TARGET_DIR)/usr/share/driverlab/tests
	$(INSTALL) -m 0644 $(@D)/lib.sh $(@D)/*.test $(TARGET_DIR)/usr/share/driverlab/tests/
endef

$(eval $(generic-package))
