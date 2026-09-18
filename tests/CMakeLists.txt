cmake_minimum_required(VERSION 3.16)
project(OpenRGBFixedRegressionTests LANGUAGES CXX)

enable_testing()
find_package(Threads REQUIRED)
option(OPENRGB_FIXED_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
get_filename_component(OPENRGB_SOURCE "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

add_executable(logitech_receiver_lifetime
    logitech_receiver_lifetime.cpp
    "${OPENRGB_SOURCE}/Controllers/LogitechController/LogitechProtocolCommon.cpp")
add_executable(device_worker_lifetime
    device_worker_lifetime.cpp
    "${OPENRGB_SOURCE}/RGBController/RGBController.cpp")
add_executable(logitech_receiver_watcher
    logitech_receiver_watcher.cpp
    "${OPENRGB_SOURCE}/Controllers/LogitechController/LogitechLightspeedController/LogitechLightspeedReceiverWatcher.cpp")
add_executable(logitech_receiver_enumeration
    logitech_receiver_enumeration.cpp
    "${OPENRGB_SOURCE}/Controllers/LogitechController/LogitechProtocolCommon.cpp")
add_executable(background_worker
    background_worker.cpp)
add_executable(logitech_lighting_control
    logitech_lighting_control.cpp
    "${OPENRGB_SOURCE}/Controllers/LogitechController/LogitechProtocolCommon.cpp")

foreach(test_target IN ITEMS logitech_receiver_lifetime device_worker_lifetime logitech_lighting_control background_worker logitech_receiver_watcher logitech_receiver_enumeration)
    target_compile_features(${test_target} PRIVATE cxx_std_17)
    target_include_directories(${test_target} PRIVATE
        "${OPENRGB_SOURCE}"
        "${OPENRGB_SOURCE}/RGBController"
        "${OPENRGB_SOURCE}/Controllers/LogitechController"
        "${OPENRGB_SOURCE}/Controllers/LogitechController/LogitechLightspeedController"
        "${OPENRGB_SOURCE}/dependencies/hidapi-win/include"
        "${OPENRGB_SOURCE}/dependencies/json")
    target_compile_definitions(${test_target} PRIVATE HID_API_NO_EXPORT_DEFINE)
    target_link_libraries(${test_target} PRIVATE Threads::Threads)
    if(MSVC)
        target_compile_options(${test_target} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${test_target} PRIVATE -UNDEBUG)
    endif()
    if(OPENRGB_FIXED_SANITIZERS)
        if(MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            message(FATAL_ERROR "Sanitizers require GCC or Clang on a Unix host")
        endif()
        target_compile_options(${test_target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${test_target} PRIVATE -fsanitize=address,undefined)
    endif()
    if(NOT test_target MATCHES "^(logitech_lighting_control|logitech_receiver_watcher|logitech_receiver_enumeration)$")
        add_test(NAME ${test_target} COMMAND ${test_target})
        set_tests_properties(${test_target} PROPERTIES TIMEOUT 20)
    endif()
endforeach()

foreach(lighting_case IN ITEMS static mode color power power_control power_changed power_retry slow_ack replies short_ack stale selector queue_boundary queue_busy errors short write timeout legacy shared control_read power_read init_locked init_slow)
    add_test(NAME logitech_lighting_${lighting_case} COMMAND logitech_lighting_control ${lighting_case})
    set_tests_properties(logitech_lighting_${lighting_case} PROPERTIES TIMEOUT 20)
endforeach()

foreach(watcher_case IN ITEMS late_create create_retry pending_linked unknown_slot asleep malformed link_reapply power_return periodic_repaint no_periodic_for_effect no_repaint_in_power_save poll_reapply quiet backoff backoff_long_poll unsupported link_down stop)
    add_test(NAME logitech_watcher_${watcher_case} COMMAND logitech_receiver_watcher ${watcher_case})
    set_tests_properties(logitech_watcher_${watcher_case} PROPERTIES TIMEOUT 20)
endforeach()

foreach(enumeration_case IN ITEMS normal boot_order asleep stale_reply enable_notifications)
    add_test(NAME logitech_enumeration_${enumeration_case} COMMAND logitech_receiver_enumeration ${enumeration_case})
    set_tests_properties(logitech_enumeration_${enumeration_case} PROPERTIES TIMEOUT 20)
endforeach()
