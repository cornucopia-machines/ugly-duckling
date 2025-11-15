cmake_minimum_required(VERSION 3.16)

include(../../Common.cmake)

set(TEST_COMPONENTS "kernel-test" "utils-test" "unit-test-support" CACHE STRING "List of components to test")

project(ugly-duckling-embedded-tests)
