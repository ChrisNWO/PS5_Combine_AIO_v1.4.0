cmake_minimum_required(VERSION 3.21)
project(PS5CombineAIO VERSION 1.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)

qt_add_executable(PS5CombineAIO WIN32
    src/main.cpp
    src/MainWindow.cpp      src/MainWindow.h
    src/JobPanel.cpp        src/JobPanel.h
    src/ToolRunner.cpp      src/ToolRunner.h
    src/I18n.cpp            src/I18n.h
    src/AppPaths.cpp        src/AppPaths.h
    src/ParamJson.cpp       src/ParamJson.h
    src/LibraryScanner.cpp  src/LibraryScanner.h
    src/ErrorHints.h
    src/Theme.cpp           src/Theme.h
    src/PathRow.h
    src/NavIcons.h
    src/pages/PageBase.cpp  src/pages/PageBase.h
    src/pages/LibraryPage.cpp src/pages/LibraryPage.h
    src/pages/PackPage.cpp  src/pages/PackPage.h
    src/pages/FpkgPage.cpp  src/pages/FpkgPage.h
    src/pages/ToolsPage.cpp src/pages/ToolsPage.h
    src/pages/TasksPage.cpp src/pages/TasksPage.h
    src/pages/EditFilesDialog.cpp src/pages/EditFilesDialog.h
    src/pages/LibraryDialogs.cpp src/pages/LibraryDialogs.h
    src/pages/CompareDialog.cpp src/pages/CompareDialog.h
    src/pages/BackportPage.cpp src/pages/BackportPage.h
    src/GameCheck.cpp src/GameCheck.h
    src/PkgSizeEstimate.cpp src/PkgSizeEstimate.h
    src/FtpUpload.cpp src/FtpUpload.h
    src/pages/SettingsPage.cpp src/pages/SettingsPage.h
    src/pages/AboutPage.cpp src/pages/AboutPage.h
    resources.qrc
)

target_include_directories(PS5CombineAIO PRIVATE src src/pages)
target_compile_definitions(PS5CombineAIO PRIVATE APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(PS5CombineAIO PRIVATE Qt6::Widgets)

if(MSVC)
    target_compile_options(PS5CombineAIO PRIVATE /W4 /utf-8)
else()
    target_compile_options(PS5CombineAIO PRIVATE -Wall -Wextra)
endif()

if(WIN32)
    target_sources(PS5CombineAIO PRIVATE resources/app.rc)
endif()
