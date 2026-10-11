cmake_minimum_required(VERSION 3.21)
project(eter VERSION 0.10.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)
find_package(Threads REQUIRED)

# Statikus, csak hangot dekódoló FFmpeg (scripts/build_ffmpeg.sh állítja elő)
set(FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/third_party/ffmpeg" CACHE PATH "FFmpeg telepítési könyvtár")
if(NOT EXISTS "${FFMPEG_ROOT}/lib/libavformat.a")
    message(FATAL_ERROR "Nincs FFmpeg itt: ${FFMPEG_ROOT}. Futtasd előbb: scripts/build_ffmpeg.sh")
endif()

add_executable(eter
    src/main.cpp
    src/i18n.cpp
    src/i18n_en.cpp
    src/audio/Decoder.cpp
    src/audio/Source.cpp
    src/audio/Engine.cpp
    src/ui/Theme.cpp
    src/ui/Widgets.cpp
    src/ui/PlayerWidget.cpp
    src/db/Database.cpp
    src/ui/MainWindow.cpp
    src/ui/Library.cpp
    src/ui/PlaylistTable.cpp
    src/ui/SchedulerPage.cpp
    src/ui/MixEditorPage.cpp
    src/ui/PlaylistEditorPage.cpp
    src/ui/CartwallPanel.cpp
    src/ui/ItemEditor.cpp
    src/sched/Scheduler.cpp
    src/ui/SettingsDialog.cpp
    resources/resources.qrc
)

target_include_directories(eter PRIVATE src third_party "${FFMPEG_ROOT}/include")
target_compile_definitions(eter PRIVATE ETER_VERSION="${PROJECT_VERSION}")
target_compile_options(eter PRIVATE -Wall -Wextra)
set_source_files_properties(src/audio/Engine.cpp PROPERTIES COMPILE_OPTIONS "-w")  # miniaudio figyelmeztetései

target_link_libraries(eter PRIVATE
    Qt6::Widgets
    Qt6::Sql
    "${FFMPEG_ROOT}/lib/libavformat.a"
    "${FFMPEG_ROOT}/lib/libavcodec.a"
    "${FFMPEG_ROOT}/lib/libswresample.a"
    "${FFMPEG_ROOT}/lib/libavutil.a"
    Threads::Threads ${CMAKE_DL_LIBS} m
)
target_link_options(eter PRIVATE -Wl,-Bsymbolic)

install(TARGETS eter RUNTIME DESTINATION bin)
install(FILES packaging/eter.desktop DESTINATION share/applications)
