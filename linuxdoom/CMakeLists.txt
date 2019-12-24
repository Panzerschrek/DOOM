cmake_minimum_required(VERSION 2.8)

project( DOOM )

find_package(Qt5 COMPONENTS Gui REQUIRED)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(WIN32)
	add_definitions( -DPANZER_DOOM_WIDNOWS )
endif()

file( GLOB SOURCES "*.h" "*.c" "*.cpp" "r_panzer/*" )

add_executable( DOOM ${SOURCES} )
target_link_libraries( DOOM Qt5::Gui )

if(WIN32)
	target_link_libraries( DOOM WS2_32 )
endif()
