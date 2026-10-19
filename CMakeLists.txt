cmake_minimum_required(VERSION 3.21)
project(eter VERSION 0.18.0 LANGUAGES C CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Network)
find_package(Threads REQUIRED)

# MP3 kódoló az adásrögzítéshez (LAME, LGPL). Statikusan linkelve, ha van (Ubuntu: libmp3lame-dev, MSYS2: lame).
find_path(LAME_INCLUDE_DIR lame/lame.h)
find_library(LAME_LIBRARY NAMES libmp3lame.a mp3lame)
if(NOT LAME_INCLUDE_DIR OR NOT LAME_LIBRARY)
    message(FATAL_ERROR "Nincs LAME (MP3 kódoló). Ubuntu: sudo apt install libmp3lame-dev; MSYS2: pacman -S mingw-w64-ucrt-x86_64-lame")
endif()

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
    src/ui/TimedEventsPage.cpp
    src/ui/MiniMix.cpp
    src/sched/Scheduler.cpp
    src/ui/SettingsDialog.cpp
    src/net/Remote.cpp
    src/ui/RemoteEditor.cpp
    src/core/FileCheck.cpp
    src/ui/MainWindowFiles.cpp
    src/ui/MainWindowTiming.cpp
    src/ui/MainWindowUpload.cpp
    src/ui/MainWindowBackup.cpp
    src/ui/BackupWindow.cpp
    src/audio/ProgramLogger.cpp
    src/audio/Analysis.cpp
    resources/resources.qrc
)

target_include_directories(eter PRIVATE src third_party "${FFMPEG_ROOT}/include" "${LAME_INCLUDE_DIR}")
target_compile_definitions(eter PRIVATE ETER_VERSION="${PROJECT_VERSION}")
target_compile_options(eter PRIVATE -Wall -Wextra)
set_source_files_properties(src/audio/Engine.cpp PROPERTIES COMPILE_OPTIONS "-w")  # miniaudio figyelmeztetései

target_link_libraries(eter PRIVATE
    Qt6::Widgets
    Qt6::Sql
    Qt6::Network
    "${FFMPEG_ROOT}/lib/libavformat.a"
    "${FFMPEG_ROOT}/lib/libavcodec.a"
    "${FFMPEG_ROOT}/lib/libswresample.a"
    "${FFMPEG_ROOT}/lib/libavutil.a"
    "${LAME_LIBRARY}"
    Threads::Threads ${CMAKE_DL_LIBS} m
)
if(WIN32)
    # Windows: ablakos program (konzol nélkül), ikonnal és verzióadatokkal; a windows.h ne definiáljon min/max makrót.
    set_target_properties(eter PROPERTIES WIN32_EXECUTABLE ON)
    target_compile_definitions(eter PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    configure_file(packaging/eter.rc.in "${CMAKE_BINARY_DIR}/eter.rc" @ONLY)
    target_sources(eter PRIVATE "${CMAKE_BINARY_DIR}/eter.rc")
    target_link_libraries(eter PRIVATE bcrypt ole32 winmm)   # a statikus FFmpeg és a miniaudio rendszerkönyvtárai
else()
    target_link_options(eter PRIVATE -Wl,-Bsymbolic)
endif()

install(TARGETS eter RUNTIME DESTINATION bin)
if(UNIX)
    install(FILES packaging/eter.desktop DESTINATION share/applications)
endif()
