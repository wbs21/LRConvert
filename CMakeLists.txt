cmake_minimum_required(VERSION 3.20)

# 版本号单一来源：src/core/Version.h（scripts/package.sh 也读它，别处不要硬编码）
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/core/Version.h" LR_VERSION_HEADER)
string(REGEX MATCH "kAppVersion *= *\"([0-9][0-9.]*)\"" _lr_version_match "${LR_VERSION_HEADER}")
if(NOT _lr_version_match)
    message(FATAL_ERROR "无法从 src/core/Version.h 解析出版本号（kAppVersion）")
endif()
project(LRConvert VERSION ${CMAKE_MATCH_1} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
endif()

# 项目内的 FFmpeg 依赖副本（bin/ = 挑选后的运行时 DLL，include/ = 编译期头文件）
#
# 注意：这里是**副本**，不直接引用外部构建目录（ffmpeg-gpl-full/ 等）。
# 挑选依据与清单见 vendor/ffmpeg/README.md：从完整的 50 个 DLL / 89.7 MB 中，
# 按「本项目实际加载的 5 个核心库」求依赖闭包，取 29 个 / 74.6 MB。
set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/vendor/ffmpeg" CACHE PATH "项目内 FFmpeg 依赖根目录")

# ===========================================================================
# FFmpeg 运行时层
#
# 关键约束（见重构方案 §6）：本工程**不链接** libav*，
# 只用它的头文件做类型定义，真正的符号在 UI 首帧显示之后由 FfmpegApi
# 通过 LoadLibrary / dlopen 解析。
# 一旦在这里 target_link_libraries 到 libav*，DLL 就会写进 PE 导入表，
# 在 main() 之前被操作系统加载，"启动要快"就无从谈起。
# ===========================================================================
add_library(ffmpeg_runtime STATIC
    src/ffmpeg/FfmpegApi.cpp
    src/ffmpeg/FfmpegCapability.cpp
    # Tab1 时间轴所需：媒体探测 / 取帧 / 音频峰值（都不依赖 Qt）
    src/ffmpeg/MediaProbe.cpp
    src/ffmpeg/FrameGrabber.cpp
    src/ffmpeg/AudioPeaks.cpp
    # 整轨波形：分段 + 多线程 + 可见区优先
    src/ffmpeg/WaveformBuilder.cpp
    # Tab1 预览播放所需：音频解码 + 声卡输出 + 播放循环（同样不依赖 Qt）
    src/ffmpeg/AudioDecoder.cpp
    src/audio/AudioOutput.cpp
    src/player/FramePlayer.cpp
)
target_include_directories(ffmpeg_runtime PUBLIC src "${FFMPEG_DIR}/include")

# 播放要用 std::thread；音频输出走 Windows 的 waveOut（winmm.dll 系统自带），
# 不引 Qt Multimedia —— 免得发布包里多带一份 Qt 自己的 FFmpeg 后端。
find_package(Threads REQUIRED)
target_link_libraries(ffmpeg_runtime PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(ffmpeg_runtime PUBLIC winmm)
endif()

# ===========================================================================
# M0 验收工具：不依赖 Qt，任何时候都能构建
#   用法：cmake -B build && cmake --build build && LRCONVERT_FFMPEG_DIR=... ./build/capability_check
# ===========================================================================
add_executable(capability_check tools/capability_check.cpp)
target_link_libraries(capability_check PRIVATE ffmpeg_runtime)

add_custom_command(TARGET capability_check POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:capability_check>/ffmpeg"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${FFMPEG_DIR}/bin" "$<TARGET_FILE_DIR:capability_check>/ffmpeg"
    COMMENT "复制 FFmpeg 动态库到 capability_check 所在目录"
)

# ===========================================================================
# Tab1 时间轴的无界面验证工具：打印媒体信息 / 关键帧 / 抓帧 / 波形峰值
#   用法：./build/media_check.exe <视频文件>
# ===========================================================================
add_executable(media_check tools/media_check.cpp)
target_link_libraries(media_check PRIVATE ffmpeg_runtime)
add_custom_command(TARGET media_check POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:media_check>/ffmpeg"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${FFMPEG_DIR}/bin" "$<TARGET_FILE_DIR:media_check>/ffmpeg"
    COMMENT "复制 FFmpeg 动态库到 media_check 所在目录"
)

# ===========================================================================
# 主程序：需要 Qt6（MinGW 版，必须与 FFmpeg 的 MinGW-w64 工具链一致）
# 未安装 Qt6 时仅跳过，不影响 capability_check 的构建与验证。
# ===========================================================================
# Qt6 定位：优先用命令行传入的 CMAKE_PREFIX_PATH / Qt6_DIR；
# 都没给时自动探测 C:/Qt/6.*/mingw_64（取版本号最大的一个）
if(NOT DEFINED Qt6_DIR AND "$ENV{CMAKE_PREFIX_PATH}" STREQUAL "" AND "${CMAKE_PREFIX_PATH}" STREQUAL "")
    file(GLOB _qt_kits "C:/Qt/6.*/mingw_64")
    if(_qt_kits)
        list(SORT _qt_kits)
        list(REVERSE _qt_kits)
        foreach(_kit IN LISTS _qt_kits)
            list(APPEND CMAKE_PREFIX_PATH "${_kit}")
        endforeach()
        message(STATUS "自动探测到 Qt6 kit：${_qt_kits}")
    endif()
endif()

find_package(Qt6 COMPONENTS Widgets Network Concurrent QUIET)

if(Qt6_FOUND)
    message(STATUS "找到 Qt6 ${Qt6_VERSION}，将构建主程序 LRConvert")
    qt_standard_project_setup()

    # qt_standard_project_setup() 只管 AUTOMOC / AUTOUIC，**不管 AUTORCC**。
    # 不开这个的话 .qrc 会被当成普通源文件丢给 C++ 编译器（甚至被忽略），
    # 运行时 QIcon(":/xxx") 恒为空 —— 表现为窗口图标、工具栏图标全都没了，
    # 而且没有任何报错，非常难查。
    set(CMAKE_AUTORCC ON)

    # Windows 资源编译器（windres）：把 resources/app.rc 里的图标链接进 exe。
    # 不这么做的话，exe 在资源管理器里就是默认的白板图标，
    # 任务栏在缺少窗口图标时也没有可回退的东西。
    if(WIN32)
        enable_language(RC)
    endif()

    qt_add_executable(LRConvert
        src/main.cpp
        src/MainWindow.h
        src/MainWindow.cpp
        # core 层：纯逻辑，不依赖 Widgets
        src/core/ITask.h
        src/core/TimeUtil.h
        src/core/TimeUtil.cpp
        src/core/PathUtil.h
        src/core/PathUtil.cpp
        src/core/Version.h
        # 任务层：真正的 libav* 管线
        src/tasks/RemuxTask.h
        src/tasks/RemuxTask.cpp
        src/tasks/TranscodeTask.h
        src/tasks/TranscodeTask.cpp
        # 界面（Qt Designer 产物，uic 自动生成 ui_mainui.h）
        src/ui/mainui.ui
        src/ui/TimelineWidget.h
        src/ui/TimelineWidget.cpp
        # 窗口外框查询（BUG-001：还原后把标题栏拉回可见区；内部用 Win32，隔离在这里）
        src/platform/WindowFrame.h
        src/platform/WindowFrame.cpp
        resources/app.qrc
    )
    target_link_libraries(LRConvert PRIVATE
        ffmpeg_runtime
        Qt6::Widgets
        Qt6::Network
        Qt6::Concurrent
    )
    set_target_properties(LRConvert PROPERTIES WIN32_EXECUTABLE ON)
    # .rc 只在 Windows 上加：别的平台没有 windres，列进源列表会让 CMake 报错
    if(WIN32)
        target_sources(LRConvert PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc")
    endif()
    # MainWindow.h 里 include 的是 "ui_mainui.h"，而 .ui 放在 src/ui/ 下，
    # 需要显式告诉 AUTOUIC 去哪里找，否则它只在源文件同目录里找。
    set_target_properties(LRConvert PROPERTIES
        AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")

    # FFmpeg 的 DLL 必须放进 exe 同目录的 ffmpeg/ **子目录**，不能直接放根目录。
    # 原因（踩过坑）：FFmpeg 会带上 zlib1.dll / libstdc++-6.dll / libbz2-1.dll 等
    # 通用运行时库。一旦它们与 exe 同级，Windows 加载器在解析 **Qt 自身依赖**
    # 时会优先命中这些同名 DLL（exe 目录优先级最高），版本对不上就直接
    # 让整个进程启动失败（退出码 127，且没有任何错误提示）。
    # 放进子目录后由 FfmpegApi 用绝对路径加载，与 Qt 的 DLL 物理隔离。
    add_custom_command(TARGET LRConvert POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LRConvert>/ffmpeg"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${FFMPEG_DIR}/bin" "$<TARGET_FILE_DIR:LRConvert>/ffmpeg"
        COMMENT "复制 FFmpeg 动态库到 LRConvert 同级的 ffmpeg/ 子目录"
    )

    # RemuxTask 的无界面验证工具：走的是与 GUI 完全相同的代码路径
    qt_add_executable(remux_check
        tools/remux_check.cpp
        src/core/ITask.h
        src/core/TimeUtil.h
        src/core/TimeUtil.cpp
        src/core/PathUtil.h
        src/core/PathUtil.cpp
        src/tasks/RemuxTask.h
        src/tasks/RemuxTask.cpp
    )
    target_link_libraries(remux_check PRIVATE
        ffmpeg_runtime
        Qt6::Core
    )
    add_custom_command(TARGET remux_check POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:remux_check>/ffmpeg"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${FFMPEG_DIR}/bin" "$<TARGET_FILE_DIR:remux_check>/ffmpeg"
        COMMENT "复制 FFmpeg 动态库到 remux_check 同级的 ffmpeg/ 子目录"
    )

    # TranscodeTask 的无界面验证工具（M5 压缩管线）
    qt_add_executable(transcode_check
        tools/transcode_check.cpp
        src/core/ITask.h
        src/tasks/TranscodeTask.h
        src/tasks/TranscodeTask.cpp
    )
    target_link_libraries(transcode_check PRIVATE
        ffmpeg_runtime
        Qt6::Core
    )
    add_custom_command(TARGET transcode_check POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:transcode_check>/ffmpeg"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${FFMPEG_DIR}/bin" "$<TARGET_FILE_DIR:transcode_check>/ffmpeg"
        COMMENT "复制 FFmpeg 动态库到 transcode_check 同级的 ffmpeg/ 子目录"
    )
else()
    message(STATUS "")
    message(STATUS "未找到 Qt6 —— 本次只构建 capability_check（不依赖 Qt）。")
    message(STATUS "安装 Qt6 的 **MinGW 版** 后重新 configure 即可构建主程序；")
    message(STATUS "注意：MSVC 版 Qt 与 MinGW-w64 编译的 FFmpeg ABI 不兼容，不能混用。")
    message(STATUS "")
endif()
