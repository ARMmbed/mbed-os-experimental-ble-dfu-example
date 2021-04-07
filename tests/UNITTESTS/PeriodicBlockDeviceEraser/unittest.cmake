
####################
# UNIT TESTS
####################

# TODO - I couldn't get this to build...

set(unittest-includes ${unittest-includes}
  .
  ../
  ../mbed-os/
  ../mbed-os/platform/include/
  ../mbed-os/platform/mbed-trace/include/
  ../mbed-os/storage/blockdevice/include/
  ../mbed-os/events/include
)

set(unittest-sources
  ../PeriodicBlockDeviceEraser.cpp
)

set(unittest-test-sources
  PeriodicBlockDeviceEraser/test_PeriodicBlockDeviceEraser.cpp
)

link_libraries(
  PRIVATE
      mbed-fakes-event-queue
      mbed-headers-base
      mbed-headers-platform
      gmock_main
)
    

set(CONF_FLAGS "-DMBED_CONF_PLATFORM_CTHUNK_COUNT_MAX=10")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${DEVICE_FLAGS} ${CONF_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${DEVICE_FLAGS} ${CONF_FLAGS}")
