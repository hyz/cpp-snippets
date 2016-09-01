LOCAL_PATH:= $(call my-dir)

### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
include $(CLEAR_VARS)
LOCAL_MODULE := boost_system
LOCAL_SRC_FILES := /BOOST_ROOT/stage/lib/libboost_system.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := boost_chrono
LOCAL_SRC_FILES := /BOOST_ROOT/stage/lib/libboost_chrono.a
include $(PREBUILT_STATIC_LIBRARY)

#LOCAL_CPPFLAGS += -fexceptions
#LOCAL_CPPFLAGS += -frtti

### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
include $(CLEAR_VARS)

#LOCAL_C_INCLUDES := bionic
LOCAL_C_INCLUDES += Include openmax
LOCAL_CFLAGS     += -Wall -O2
LOCAL_CXXFLAGS   += -std=c++11
LOCAL_LDLIBS     := -L$(LOCAL_PATH)/lib -llog
LOCAL_LDLIBS     += -lm -llog  -landroid -lGLESv2  -lGLESv1_CM  -lEGL
# /boost//system /boost//chrono

LOCAL_SRC_FILES := hgs-rtph264.cpp hgs-jni.cpp VideoRender.cpp ildec.cpp OMXMaster.cpp
LOCAL_STATIC_LIBRARIES := boost_system boost_chrono

LOCAL_CPPFLAGS += -DENABLE_TEST_JNICALL
LOCAL_MODULE := hgs
include $(BUILD_SHARED_LIBRARY)

#LOCAL_CPPFLAGS += -DENABLE_TEST_MAIN -DALOG_CONSOLE # -DTEST_WITH_VIDEO_FILE
#LOCAL_MODULE := a
#include $(BUILD_EXECUTABLE)

### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
# include $(CLEAR_VARS)
# LOCAL_MODULE    := hgs
# 
# LOCAL_SRC_FILES := armeabi-v7a/libhgs.so
# include $(PREBUILT_SHARED_LIBRARY)
# 
# #LOCAL_SRC_FILES := armeabi-v7a/libhgs.a
# #include $(PREBUILT_STATIC_LIBRARY)
#

### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---
#include $(BUILD_EXECUTABLE)
#// ifneq ($(TARGET_SIMULATOR),true) ### TARGET_SIMULATOR != true
#
#// endif  # TARGET_SIMULATOR != true

