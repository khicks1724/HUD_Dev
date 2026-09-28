# ESP-IDF project for the Waveshare ESP32-S3-LCD-1.3-C (prism) TAK HUD.
# Tested API level: ESP-IDF v5.3 / v5.4 (uses the i2c_master driver, IDF >= 5.2).
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tak_hud)
