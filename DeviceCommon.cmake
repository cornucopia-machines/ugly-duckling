include(${CMAKE_CURRENT_LIST_DIR}/Common.cmake)

if(NOT DEFINED UD_GEN)
    set(UD_GEN "$ENV{UD_GEN}")
endif()
string(TOLOWER "${UD_GEN}" UD_GEN_LOWER)

# Make sure we reconfigure if UD_GEN changes
set_property(GLOBAL PROPERTY UD_GEN_TRACKER "${UD_GEN}")

# Determine the expected target for this platform/generation
set(ESP32_S3_GENS MK5 MK6 MK6_REV1 MK6_REV2 MK6_REV3 MK7 MK8 MK8_REV1 MK8_REV2)
set(ESP32_C6_GENS MKX)

if(UD_GEN STREQUAL "")
    # No generation override — infer platform from IDF_TARGET
    if("$ENV{IDF_TARGET}" STREQUAL "esp32s3")
        set(_ud_platform "esp32s3")
    elseif("$ENV{IDF_TARGET}" STREQUAL "esp32c6")
        set(_ud_platform "esp32c6")
    else()
        message(FATAL_ERROR "Error: IDF_TARGET='$ENV{IDF_TARGET}' is not a supported platform")
    endif()
    message("Building Ugly Duckling for platform '${_ud_platform}' (runtime MAC detection)")
elseif(UD_GEN IN_LIST ESP32_S3_GENS)
    set(_ud_platform "esp32s3")
    message("Building Ugly Duckling '${UD_GEN}' (forced via UD_GEN)")
elseif(UD_GEN IN_LIST ESP32_C6_GENS)
    set(_ud_platform "esp32c6")
    message("Building Ugly Duckling '${UD_GEN}' (forced via UD_GEN)")
else()
    message(FATAL_ERROR "Error: Unrecognized Ugly Duckling generation '${UD_GEN}'")
endif()

if(NOT "$ENV{IDF_TARGET}" STREQUAL "${_ud_platform}")
    message(FATAL_ERROR "Error: IDF_TARGET='$ENV{IDF_TARGET}' does not match expected platform '${_ud_platform}'")
endif()

if(NOT DEFINED UD_DEBUG)
    set(UD_DEBUG "$ENV{UD_DEBUG}")
endif()
if(UD_DEBUG STREQUAL "")
    message("UD_DEBUG is not set, assuming 0.")
    set(UD_DEBUG 0)
endif()

# Make sure we reconfigure if UD_DEBUG changes
set_property(DIRECTORY PROPERTY UD_DEBUG_TRACKER "${UD_DEBUG}")

set(SDKCONFIG_FILES)
list(APPEND SDKCONFIG_FILES "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
list(APPEND SDKCONFIG_FILES "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.${_ud_platform}.defaults")

# Check if UD_DEBUG is defined
if (UD_DEBUG)
    message("Building with debug options")
    # Add the debug-specific SDKCONFIG file to the list
    list(APPEND SDKCONFIG_FILES "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.debug.defaults")
    set(CMAKE_BUILD_TYPE Debug)
else()
    message("Building with release options")
    set(CMAKE_BUILD_TYPE Release)
endif()

list(JOIN SDKCONFIG_FILES ";" SDKCONFIG_DEFAULTS)

add_link_options("-Wl,--gc-sections")
