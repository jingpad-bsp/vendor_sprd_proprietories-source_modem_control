LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

ifeq ($(BOARD_SECURE_BOOT_ENABLE), true)
    LOCAL_C_INCLUDES := \
        vendor/sprd/proprietories-source/sprd_verify
endif

LOCAL_SRC_FILES:= \
    main.c \
    nv_read.c \
    modem_connect.c \
    modem_load.c \
    modem_control.c \
    xml_parse.c \
    modem_head_parse.c \
    modem_io_control.c \
    secure_boot_load.c \
    eventmonitor.c

ifeq ($(strip $(BOARD_EXTERNAL_MODEM)), true)
    LOCAL_SRC_FILES += external_modem_control.c \
                       modem_event.c
else
    LOCAL_SRC_FILES += internal_modem_control.c
endif

ifeq ($(strip $(USE_SPRD_ORCA_MODEM)), true)
    LOCAL_SRC_FILES += modem_pcie_control.c
endif

ifeq ($(BOARD_SIMLOCK_AP_READ_EFUSE), true)
    LOCAL_SRC_FILES += modem_simlock.c
endif

ifeq ($(BOARD_SECURE_BOOT_ENABLE), true)
    LOCAL_SRC_FILES += modem_verify.c
    LOCAL_STATIC_LIBRARIES += libsprd_verify
endif

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libexpat \
    liblog \
    libhardware_legacy

ifeq ($(strip $(BOARD_EXTERNAL_MODEM)), true)
  LOCAL_CFLAGS += -DFEATURE_EXTERNAL_MODEM
endif

ifeq ($(strip $(BOARD_REMOVE_SPRD_MODEM)), true)
  LOCAL_CFLAGS += -DFEATURE_REMOVE_SPRD_MODEM
endif

ifeq ($(BOARD_SECURE_BOOT_ENABLE), true)
  LOCAL_CFLAGS += -DSECURE_BOOT_ENABLE
endif

ifeq ($(strip $(SHARKL5_CDSP_FLAG)),true)
    LOCAL_CFLAGS += -DSHARKL5_CDSP
endif

ifeq ($(strip $(NOT_VERIFY_MODEM_FLAG)),true)
    LOCAL_CFLAGS += -DNOT_VERIFY_MODEM
endif

#Temporary bypass secure boot for kernelbootcp
ifeq ($(strip $(PRODUCT_SECURE_BOOT)),SPRD)
ifneq ($(strip $(BOARD_KBC_BYPASS_SECURE_BOOT)),true)
ifeq ($(strip $(PRODUCT_VBOOT)),V2)
    LOCAL_CFLAGS += -DCONFIG_VBOOT_V2
    ifeq ($(strip $(BOARD_TEE_CONFIG)),trusty)
        LOCAL_C_INCLUDES += vendor/sprd/proprietories-source/sprdtrusty/vendor/sprd/modules/kernelbootcp_ca
        LOCAL_SHARED_LIBRARIES += libkernelbootcp.trusty
    else ifeq ($(strip $(BOARD_TEE_CONFIG)),watchdata)
        #LOCAL_CFLAGS += -DCONFIG_SPRD_SECBOOT_WATCHDATA
        LOCAL_C_INCLUDES += vendor/sprd/partner/watchdata/isharkl2/libkernelbootcp \
                            vendor/sprd/partner/watchdata/isharkl2/linux/driver/include/wd_tee
        LOCAL_SHARED_LIBRARIES += libkernelbootcp
    endif
endif
endif
endif

ifeq ($(BOARD_SIMLOCK_AP_READ_EFUSE), true)
ifeq ($(strip $(BOARD_TEE_CONFIG)), watchdata)
     LOCAL_CFLAGS += -DSIMLOCK_AP_READ_EFUSE
     LOCAL_SHARED_LIBRARIES  += libteeproduction
else ifeq ($(strip $(BOARD_TEE_CONFIG)), beanpod)
     LOCAL_CFLAGS += -DSIMLOCK_AP_READ_EFUSE
     LOCAL_SHARED_LIBRARIES  += libteeproduction
else ifeq ($(strip $(BOARD_TEE_CONFIG)), trusty)
ifeq ($(strip $(TRUSTY_PRODUCTION)),true)
     LOCAL_CFLAGS += -DSIMLOCK_AP_READ_EFUSE
     LOCAL_SHARED_LIBRARIES  += libteeproduction
endif
endif
endif

LOCAL_MODULE := modem_control

LOCAL_INIT_RC := modem_control.rc

LOCAL_MODULE_TAGS := optional

LOCAL_PROPRIETARY_MODULE := true

include $(BUILD_EXECUTABLE)


ifeq ($(strip $(USE_SPRD_ORCA_MODEM)), true)
# modem_control debug tool
include $(CLEAR_VARS)
LOCAL_MODULE := modem_ctrl_dbg
LOCAL_PROPRIETARY_MODULE := true
LOCAL_SRC_FILES := modem_ctrl_dbg.c \
                   modem_io_control.c

ifeq ($(strip $(BOARD_EXTERNAL_MODEM)), true)
  LOCAL_CFLAGS += -DFEATURE_EXTERNAL_MODEM
endif

LOCAL_SHARED_LIBRARIES := libc \
                          libcutils \
                          liblog \
                          libutils
include $(BUILD_EXECUTABLE)

CUSTOM_MODULES += modem_ctrl_dbg
endif
