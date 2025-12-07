# EconetWiFi
# Copyright (c) 2025 Paul G. Banks <https://paulbanks.org/projects/econet>
# 
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# 
# See the LICENSE file in the project root for full license information.

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(EconetWiFi)

# Build web interface
option(WEB_BUILD "Enable building of web pages" ON)
if (WEB_BUILD)
    find_program(PNPM_EXECUTABLE NAMES pnpm)
    if (NOT PNPM_EXECUTABLE)
        message(FATAL_ERROR 
"
=================================================
  Building the Web Interface requires pnpm and 
  NodeJS. WEB_BUILD=ON but pnpm was not found.
  
  You can either:
    * Install NodeJS >=22 and pnpm and build the web interface locally or;
    * Run the build with -DWEB_BUILD=OFF and use the pre-built web
      interface from the git release corresponding to this source 
      tree. If you do that, unzip it into rootfs.
=================================================
")
    endif()

    add_custom_target(build_web ALL
        COMMAND pnpm build
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/web
    )
    add_dependencies(littlefs_rootfs_bin build_web)
endif()

