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
endif()


 add_executable (deadly-dave "main.c" "game.c" "display.c" "invfreq.c" "monster.c" "plasma.c" "tile.c" "dave.c" "soundfx.c" "bullet.c")

target_link_libraries(deadly-dave PUBLIC SDL3::SDL3-static)
#set_property(TARGET deadly-dave)
