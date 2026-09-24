cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(dash)

add_compile_definitions(CONFIG_BSP_LCD_TYPE_1024_600=1)
