cmake_minimum_required(VERSION 3.16.0)

include(DeviceCommon.cmake)

project(ugly-duckling)

# This has to be here in order for the ESP-IDF VSCode extension to recognize the project as an ESP-IDF project
# See https://github.com/espressif/vscode-esp-idf-extension/issues/1756
# include($ENV{IDF_PATH}/tools/cmake/project.cmake)
