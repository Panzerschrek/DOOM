cmake_minimum_required(VERSION 2.8)

project( DOOM )

find_package( SDL2 REQUIRED )

file( GLOB SOURCES "*.h" "*.c" "r_panzer/*" )

add_executable( DOOM ${SOURCES} )
target_include_directories( DOOM PRIVATE ${SDL2_INCLUDE_DIRS} )
target_link_libraries( DOOM PRIVATE ${SDL2_LIBRARIES} )

if( UNIX )
	target_link_libraries( DOOM PRIVATE m )
endif()
