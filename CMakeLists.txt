cmake_minimum_required(VERSION 3.10)
project(drs_muones C CXX)

# La configuracion de la medicion NO esta aca: esta en src/config.h

set(DRS4EB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../drs4eb/software"
    CACHE PATH "Directorio software/ del repo drs4eb")

# Compilar optimizado si no se pide otra cosa
if(NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de build" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${DRS4EB_DIR}/cmake")

add_executable(drs_muones
   src/main.cpp
   src/config.h
   src/filtro.h
   src/binformat.h
   ${DRS4EB_DIR}/src/DRS.cpp
   ${DRS4EB_DIR}/src/averager.cpp
   ${DRS4EB_DIR}/src/musbstd.c
   ${DRS4EB_DIR}/src/strlcpy.cxx
   ${DRS4EB_DIR}/mxml/mxml.cxx)

find_package(libusb-1.0 REQUIRED)

target_include_directories(drs_muones PRIVATE
   src
   ${DRS4EB_DIR}/include
   ${DRS4EB_DIR}/mxml
   ${LIBUSB_1_INCLUDE_DIRS})

target_compile_definitions(drs_muones PRIVATE
   OS_LINUX HAVE_USB HAVE_LIBUSB10 _FILE_OFFSET_BITS=64)

# Avisos del compilador solo para nuestro codigo (la libreria oficial da muchos)
set_source_files_properties(src/main.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
set_source_files_properties(
   ${DRS4EB_DIR}/src/DRS.cpp ${DRS4EB_DIR}/src/averager.cpp ${DRS4EB_DIR}/src/musbstd.c
   ${DRS4EB_DIR}/src/strlcpy.cxx ${DRS4EB_DIR}/mxml/mxml.cxx
   PROPERTIES COMPILE_OPTIONS "-w")

target_link_libraries(drs_muones PRIVATE ${LIBUSB_1_LIBRARIES} pthread)
