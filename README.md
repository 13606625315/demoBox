
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
writer.startRecording("./videos");

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
