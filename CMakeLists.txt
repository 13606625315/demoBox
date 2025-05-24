cmake_minimum_required(VERSION 3.10)
project(H264MP4Writer VERSION 1.0)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


add_compile_options(-g -O0)  # 添加调试符号，禁用优化
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -g")

# 添加编译选项，确保兼容性
add_compile_options(-D_GLIBCXX_USE_CXX11_ABI=0)
# 添加GPAC配置宏定义
add_definitions(-DGPAC_HAVE_CONFIG_H)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 添加源文件
set(SOURCES
    H264MP4Writer.cpp
    main.cpp
)

# 添加头文件
set(HEADERS
    H264MP4Writer.h
)

# 添加可执行文件
add_executable(mp4demo ${SOURCES} ${HEADERS})

# 查找GPAC库
find_library(GPAC_LIBRARY NAMES gpac_static libgpac_static PATHS ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT GPAC_LIBRARY)
    message(FATAL_ERROR "GPAC library not found. Please make sure libgpac_static.a is in the project directory.")
endif()

# 添加GPAC库的包含目录
# 注意：如果GPAC头文件不在标准路径，请修改此处
find_path(GPAC_INCLUDE_DIR gpac/isomedia.h
    PATHS /usr/include /usr/local/include ${CMAKE_CURRENT_SOURCE_DIR}/include
    DOC "GPAC include directory")
if(NOT GPAC_INCLUDE_DIR)
    message(WARNING "GPAC include directory not found. Using default paths.")
endif()

# 添加包含目录
target_include_directories(mp4demo PRIVATE ${GPAC_INCLUDE_DIR})

# 链接库
target_link_libraries(mp4demo PRIVATE ${GPAC_LIBRARY})

# 在Linux上可能需要额外的库
if(UNIX AND NOT APPLE)
    target_link_libraries(mp4demo PRIVATE pthread dl z)
endif()

# 创建videos目录
add_custom_command(TARGET mp4demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/videos
    COMMENT "Creating videos directory")

# 安装目标
install(TARGETS mp4demo DESTINATION bin)
install(FILES ${HEADERS} DESTINATION include)

# 添加README.md
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/README.md "
# H264MP4Writer 库

这是一个用于将H264视频流写入MP4文件的C++库，基于gpac的MP4box实现。

## 功能特点

- 支持H264 H265视频流写入MP4文件
- 自动根据开始时间生成文件名（格式：YYYYMMDD_HHMMSS.mp4）
- 提供简单易用的API接口
- 支持关键帧标记
- 自动处理SPS/PPS等参数

## 依赖项

- gpac的MP4box库libgpac_static.a
- C++17标准库

## 编译方法

### 安装GPAC库

在编译此项目前，您需要先安装GPAC库。

从源码编译：`https://github.com/gpac/gpac.git`

### 编译项目

```bash
mkdir build
cd build
cmake ..
make
```

如果库不在标准路径，请修改CMakeLists.txt中的相关路径。

## 使用方法

```cpp
// 创建H264MP4Writer实例
H264MP4Writer writer;

// 初始化参数 (宽度, 高度, 帧率)
writer.init(1920, 1080, 30);

// 开始录制，文件将保存在指定目录
writer.startRecording(\"./videos\");

// 写入H264帧数据
// frameData: H264 NALU数据，包含起始码 0x00 0x00 0x00 0x01
// frameSize: 数据大小
// isKeyFrame: 是否是关键帧
// timestamp: 时间戳（可选）
writer.writeH264Frame(frameData, frameSize, isKeyFrame);

// 停止录制
writer.stopRecording();

// 获取当前文件路径
std::string filePath = writer.getCurrentFilePath();
```

## 注意事项

- 输入的H264数据必须包含完整的NALU起始码（0x00 0x00 0x00 0x01）
- 为了正确生成MP4文件，应该在开始时提供SPS和PPS NALU
- 关键帧标记对于正确播放MP4文件非常重要

## 许可证

此项目采用MIT许可证。
")