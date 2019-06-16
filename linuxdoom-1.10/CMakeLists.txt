cmake_minimum_required(VERSION 2.8)

project( DOOM )

file( GLOB SOURCES "*.h" "*.c" "r_panzer/*" )

add_executable( DOOM ${SOURCES} )

if( UNIX )
	target_include_directories( DOOM PRIVATE /usr/include/SDL2 )
	target_link_libraries( DOOM PRIVATE SDL2 m )
else()
	message( FATAL_ERROR " TODO - set paths to your SDL2 installation " )
endif()

