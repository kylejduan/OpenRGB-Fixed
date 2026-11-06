cmake_minimum_required(VERSION 3.16)
project(OpenRGBFixedRegressionTests LANGUAGES CXX)

enable_testing()
find_package(Threads REQUIRED)
option(OPENRGB_FIXED_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
option(OPENRGB_FIXED_TSAN "Enable the thread sanitizer" OFF)
if(OPENRGB_FIXED_SANITIZERS AND OPENRGB_FIXED_TSAN)
    message(FATAL_ERROR "The address and thread sanitizers cannot be combined")
endif()
get_filename_component(OPENRGB_SOURCE "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

add_executable(logitech_receiver_lifetime
    logitech_receiver_lifetime.cpp
    "${OPENRGB_SOURCE}/Controllers/LogitechController/LogitechProtocolCommon.cpp")
add_executable(device_worker_lifetime
    device_worker_lifetime.cpp
    "${OPENRGB_SOURCE}/RGBController/RGBController.cpp")
add_executable(device_worker_notifications
    device_worker_notifications.cpp
    "${OPENRGB_SOURCE}/RGBController/RGBController.cpp")
add_executable(corsair_v2_keepalive
    corsair_v2_keepalive.cpp
    "${OPENRGB_SOURCE}/RGBController/RGBController.cpp"
    "${OPENRGB_SOURCE}/Controllers/CorsairPeripheralV2Controller/RGBController_CorsairV2Hardware.cpp"
    "${OPENRGB_SOURCE}/RGBController/RGBControllerKeyNames.cpp"
    "${OPENRGB_SOURCE}/KeyboardLayoutManager/KeyboardLayoutManager.cpp")
add_executable(corsair_v2_drain
    corsair_v2_drain.cpp
    "${OPENRGB_SOURCE}/Controllers/CorsairPeripheralV2Controller/CorsairPeripheralV2Controller.cpp"
    "${OPENRGB_SOURCE}/StringUtils.cpp")
add_executable(corsair_v2_packet
    corsair_v2_packet.cpp
    "${OPENRGB_SOURCE}/Controllers/CorsairPeripheralV2Controller/CorsairPeripheralV2Controller.cpp"
    "${OPENRGB_SOURCE}/Controllers/CorsairPeripheralV2Controller/CorsairPeripheralV2HardwareController.cpp"
    "${OPENRGB_SOURCE}/StringUtils.cpp")
add_executable(log_manager
    log_manager.cpp
    "${OPENRGB_SOURCE}/LogManager.cpp")
target_compile_definitions(log_manager PRIVATE VERSION_STRING="fixture" GIT_COMMIT_ID="fixture" GIT_COMMIT_DATE="fixture")
add_executable(logitech_receiver_watcher
    logitech_receiver_watcher.cpp
    "${OPENRGB_SOURCE}/Controllers/LogitechController/LogitechLightspeedController/LogitechLightspeedReceiverWatcher.cpp")
add_executable(logitech_receiver_enumeration
    logitech_receiver_enumeration.cpp
    "${OPENRGB_SOURCE}/Controllers/LogitechController/LogitechProtocolCommon.cpp")
add_executable(background_worker
    background_worker.cpp)
add_executable(logitech_connection_query
    logitech_connection_query.cpp
    "${OPENRGB_SOURCE}/Controllers/LogitechController/LogitechProtocolCommon.cpp")
add_executable(logitech_lightspeed_modes
    logitech_lightspeed_modes.cpp
    "${OPENRGB_SOURCE}/RGBController/RGBController.cpp"
    "${OPENRGB_SOURCE}/StringUtils.cpp"
    "${OPENRGB_SOURCE}/Controllers/LogitechController/LogitechProtocolCommon.cpp"
    "${OPENRGB_SOURCE}/Controllers/LogitechController/LogitechLightspeedController/LogitechLightspeedController.cpp"
    "${OPENRGB_SOURCE}/Controllers/LogitechController/LogitechLightspeedController/RGBController_LogitechLightspeed.cpp")
add_executable(logitech_lighting_control
    logitech_lighting_control.cpp
    "${OPENRGB_SOURCE}/Controllers/LogitechController/LogitechProtocolCommon.cpp")

foreach(test_target IN ITEMS logitech_receiver_lifetime device_worker_lifetime device_worker_notifications corsair_v2_keepalive corsair_v2_drain corsair_v2_packet log_manager logitech_lighting_control background_worker logitech_receiver_watcher logitech_receiver_enumeration logitech_connection_query logitech_lightspeed_modes)
    target_compile_features(${test_target} PRIVATE cxx_std_17)
    target_include_directories(${test_target} PRIVATE
        "${OPENRGB_SOURCE}"
        "${OPENRGB_SOURCE}/RGBController"
        "${OPENRGB_SOURCE}/Controllers/LogitechController"
        "${OPENRGB_SOURCE}/Controllers/LogitechController/LogitechLightspeedController"
        "${OPENRGB_SOURCE}/Controllers/CorsairPeripheralV2Controller"
        "${OPENRGB_SOURCE}/KeyboardLayoutManager"
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
    if(OPENRGB_FIXED_TSAN)
        target_compile_definitions(${test_target} PRIVATE OPENRGB_FIXED_TSAN)
        if(MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            message(FATAL_ERROR "The thread sanitizer requires GCC or Clang on a Unix host")
        endif()
        target_compile_options(${test_target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(${test_target} PRIVATE -fsanitize=thread)
    endif()
    if(NOT test_target MATCHES "^(log_manager|corsair_v2_keepalive|corsair_v2_drain|device_worker_notifications|logitech_lighting_control|logitech_receiver_watcher|logitech_receiver_enumeration|logitech_connection_query)$")
        add_test(NAME ${test_target} COMMAND ${test_target})
        set_tests_properties(${test_target} PROPERTIES TIMEOUT 20)
    endif()
endforeach()

# Make initialization regressions deterministic instead of relying on the
# contents of previously used stack memory. This applies only to the fixture.
if(NOT MSVC)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-ftrivial-auto-var-init=pattern" OPENRGB_FIXED_AUTO_VAR_INIT)
    if(OPENRGB_FIXED_AUTO_VAR_INIT)
        target_compile_options(corsair_v2_drain PRIVATE -ftrivial-auto-var-init=pattern)
    endif()
endif()

foreach(drain_case IN ITEMS error flood)
    add_test(NAME corsair_drain_${drain_case} COMMAND corsair_v2_drain ${drain_case})
    set_tests_properties(corsair_drain_${drain_case} PROPERTIES TIMEOUT 20)
endforeach()

add_test(NAME corsair_initialization COMMAND corsair_v2_drain init)
set_tests_properties(corsair_initialization PROPERTIES TIMEOUT 20)

foreach(corsair_case IN ITEMS stop serialize arming pending)
    add_test(NAME corsair_keepalive_${corsair_case} COMMAND corsair_v2_keepalive ${corsair_case})
    set_tests_properties(corsair_keepalive_${corsair_case} PROPERTIES TIMEOUT 20)
endforeach()

foreach(worker_case IN ITEMS wake wake_default reentry coalesce ordering ordering_default reset_after stop stop_default contention profile profile_effect profile_invalid profile_empty)
    add_test(NAME device_worker_${worker_case} COMMAND device_worker_notifications ${worker_case})
    set_tests_properties(device_worker_${worker_case} PROPERTIES TIMEOUT 20)
endforeach()

foreach(lighting_case IN ITEMS static mode color power power_control power_changed power_retry slow_ack replies short_ack stale selector queue_boundary queue_busy errors short write timeout legacy shared control_read power_read init_locked init_slow init_8070 init_8070_slow init_8070_unrelated)
    add_test(NAME logitech_lighting_${lighting_case} COMMAND logitech_lighting_control ${lighting_case})
    set_tests_properties(logitech_lighting_${lighting_case} PROPERTIES TIMEOUT 20)
endforeach()

foreach(watcher_case IN ITEMS late_create create_retry create_retry_forever create_retry_link_down receiver_lost receiver_recovers contention contention_repaint repaint_keeps_poll shared_reads shared_link_down pending_linked unknown_slot asleep malformed link_reapply power_return periodic_repaint no_periodic_for_effect no_repaint_in_power_save poll_reapply quiet backoff backoff_long_poll unsupported link_down stop)
    add_test(NAME logitech_watcher_${watcher_case} COMMAND logitech_receiver_watcher ${watcher_case})
    set_tests_properties(logitech_watcher_${watcher_case} PROPERTIES TIMEOUT 20)
endforeach()

foreach(enumeration_case IN ITEMS normal boot_order asleep stale_reply enable_notifications)
    add_test(NAME logitech_enumeration_${enumeration_case} COMMAND logitech_receiver_enumeration ${enumeration_case})
    set_tests_properties(logitech_enumeration_${enumeration_case} PROPERTIES TIMEOUT 20)
endforeach()

foreach(connection_case IN ITEMS connected error unrelated silent flood)
    add_test(NAME logitech_connection_${connection_case} COMMAND logitech_connection_query ${connection_case})
    set_tests_properties(logitech_connection_${connection_case} PROPERTIES TIMEOUT 20)
endforeach()

foreach(log_case IN ITEMS singleton buffered filtered console stdout critical disabled disabled_console failed_file failed_file_console)
    add_test(NAME log_manager_${log_case} COMMAND log_manager ${log_case})
    set_tests_properties(log_manager_${log_case} PROPERTIES TIMEOUT 20)
endforeach()
