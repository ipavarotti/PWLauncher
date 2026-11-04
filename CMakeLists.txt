cmake_minimum_required(VERSION 3.15)
project(PWLauncher LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

file(GLOB LAUNCHER_SRC "${CMAKE_SOURCE_DIR}/src/*.cpp")
file(GLOB ZLIB_SRC "${CMAKE_SOURCE_DIR}/third_party/zlib/*.c")

add_executable(PWLauncher WIN32
	${LAUNCHER_SRC}
	${ZLIB_SRC}
	src/app.rc
)

target_include_directories(PWLauncher PRIVATE
	"${CMAKE_SOURCE_DIR}/src"
	"${CMAKE_SOURCE_DIR}/third_party/zlib"
)

target_compile_definitions(PWLauncher PRIVATE
	UNICODE _UNICODE
	WIN32_LEAN_AND_MEAN
	NOMINMAX
	_CRT_SECURE_NO_WARNINGS
)

set_source_files_properties(${ZLIB_SRC} PROPERTIES COMPILE_OPTIONS "-w")

if(MSVC)
	target_compile_options(PWLauncher PRIVATE /utf-8 /W3)
	target_link_libraries(PWLauncher PRIVATE
		gdiplus wininet shlwapi crypt32 shell32 ole32 comctl32 user32 gdi32 uuid)
else()
	target_compile_options(PWLauncher PRIVATE -Wall -Wno-unused-parameter -Wno-unused-variable)
	target_link_options(PWLauncher PRIVATE -municode -static -static-libgcc -static-libstdc++)
	target_link_libraries(PWLauncher PRIVATE
		gdiplus wininet shlwapi crypt32 shell32 ole32 comctl32 user32 gdi32 uuid)
endif()

add_custom_command(TARGET PWLauncher POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_SOURCE_DIR}/assets"
		"$<TARGET_FILE_DIR:PWLauncher>"
)
