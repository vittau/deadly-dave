cmake_minimum_required(VERSION 3.20)
project ("deadly-dave")

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)
find_package(OpenGL REQUIRED)


#==============================================================
#          SDL3
#=============================================================
set( SDL_STATIC ON CACHE BOOL "" FORCE )
set( SDL_SHARED OFF CACHE BOOL "" FORCE )

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY  https://github.com/libsdl-org/SDL
    GIT_TAG         release-3.4.16
    GIT_SHALLOW     TRUE
#    GIT_PROGRESS    TRUE
    OVERRIDE_FIND_PACKAGE
)

FetchContent_MakeAvailable(SDL3)

message(STATUS "SDL3 Source Dir: ${SDL3_SOURCE_DIR}")
message(STATUS "SDL3 Binary Dir: ${SDL3_BINARY_DIR}")
#=========================================================================


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${deadly-dave_SOURCE_DIR}/")
set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)

include_directories(include)

if (WIN32)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")# Add source to this project's executable.

  # Embed assets/icon.ico in the executable, which is what Explorer shows.
  enable_language(RC)
  file(TO_NATIVE_PATH "${deadly-dave_SOURCE_DIR}/assets/icon.ico" ICON_PATH)
  # rc.exe reads backslash escapes inside string literals, so a native path has
  # to reach the .rc with every backslash doubled or it eats the next letter.
  string(REPLACE "\\" "\\\\" ICON_PATH "${ICON_PATH}")
  configure_file("${deadly-dave_SOURCE_DIR}/assets/deadly-dave.rc.in"
                 "${deadly-dave_BINARY_DIR}/deadly-dave.rc" @ONLY)
  set(DEADLY_DAVE_RC "${deadly-dave_BINARY_DIR}/deadly-dave.rc")
endif()

set(DEADLY_DAVE_SOURCES "main.c" "game.c" "config.c" "highscore.c" "display.c" "filter.c"
    "composite.c" "ntsc.c" "invfreq.c" "monster.c" "plasma.c" "tile.c" "dave.c" "soundfx.c"
    "bullet.c")
if (WIN32)
  list(APPEND DEADLY_DAVE_SOURCES "${DEADLY_DAVE_RC}")
endif()

add_executable (deadly-dave ${DEADLY_DAVE_SOURCES})

target_link_libraries(deadly-dave PUBLIC SDL3::SDL3-static)
# The Blargg NTSC filter uses sin/cos/pow/exp; MSVC has them in its CRT without
# an extra library.
if (NOT WIN32)
  target_link_libraries(deadly-dave PUBLIC m)
endif()
#set_property(TARGET deadly-dave)
