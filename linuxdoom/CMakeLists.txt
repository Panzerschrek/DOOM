cmake_minimum_required(VERSION 2.8)

project( DOOM )

file( GLOB SOURCES "*.h" "*.c" "r_panzer/*" )
add_library( DOOM_lib ${SOURCES} )
target_include_directories( DOOM_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} )

if(WIN32)
	target_compile_definitions( DOOM_lib PUBLIC -DPANZER_DOOM_WIDNOWS )
	target_link_libraries( DOOM_lib WS2_32 )
else()
	target_link_libraries( DOOM_lib PUBLIC m )
endif()

find_package( SDL2 )
if( SDL2_FOUND )

	file( GLOB SOURCES_SDL "sdl/*" )

	add_executable( DOOM ${SOURCES_SDL} )
	target_include_directories( DOOM PRIVATE ${SDL2_INCLUDE_DIRS} )
	target_link_libraries( DOOM PRIVATE DOOM_lib ${SDL2_LIBRARIES} )

endif()

find_package( Qt5 COMPONENTS Gui Multimedia )
if( Qt5_FOUND )

	file( GLOB SOURCES_QT "qt/*" )

	add_executable( DOOM_qt ${SOURCES_QT} )
	target_link_libraries( DOOM_qt DOOM_lib Qt5::Gui Qt5::Multimedia )

endif()
