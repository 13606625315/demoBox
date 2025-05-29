#ifndef H264MP4_WRITER_H
#define H264MP4_WRITER_H

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <cstdint>
#include <cstddef>


#include "gpac/isomedia.h"
#include "gpac/dash.h"


/**
 * H264MP4Writer - 将H264/H265视频流写入MP4文件的类
 * 
 * 基于GPAC的MP4Box实现，支持H264和H265视频流
 */
class H264MP4Writer {
public:
    H264MP4Writer();
    ~H264MP4Writer();

    /**
     * 初始化视频参数
     * 
     * @param width 视频宽度
     * @param height 视频高度
     * @param frameRate 帧率
     * @param isH265 是否为H265编码（默认为-1，表示自动检测编码类型）
     * @return 是否初始化成功
     */
    bool init(int width, int height, float frameRate, int isH265 = -1);

    /**
     * 开始录制
     * 
     * @param outputDir 输出目录
     * @return 是否成功开始录制
     */
    bool startRecording(const std::string& outputDir);

    /**
     * 停止录制
     * 
     * @return 是否成功停止录制
     */
    bool stopRecording();

    /**
     * 写入H264/H265帧数据
     * 
     * @param frameData 帧数据（包含起始码 0x00 0x00 0x00 0x01）
     * @param frameSize 数据大小
     * @param isKeyFrame 是否是关键帧
     * @param timestamp 时间戳（毫秒，可选）
     * @return 是否成功写入
     */
    bool writeFrame(const uint8_t* frameData, size_t frameSize, bool isKeyFrame, int64_t timestamp = -1);


    /**
     * 获取当前文件路径
     * 
     * @return 当前MP4文件的完整路径
     */
    std::string getCurrentFilePath() const;

    /**
     * 检查是否正在录制
     * 
     * @return 是否正在录制
     */
    bool isRecording() const { return m_isRecording; }

    /**
     * 初始化分段MP4（用于DASH流）
     * 
     * @param width 视频宽度
     * @param height 视频高度
     * @param frameRate 帧率
     * @param isH265 是否为H265编码（默认为-1，表示自动检测编码类型）
     * @param outputDir 输出目录
     * @return 是否初始化成功
     */
    bool initFragmentedMP4(int width, int height, float frameRate, int isH265 = -1, const std::string& outputDir = "./dash");

    /**
     * 开始新的分段
     * 
     * @param fragmentDuration 分段时长（毫秒）
     * @return 是否成功开始分段
     */
    bool startFragment(uint32_t fragmentDuration = 1000);

    /**
     * 结束当前分段
     * 
     * @return 是否成功结束分段
     */
    bool endFragment();

    /**
     * 生成DASH MPD文件
     * 
     * @param streamName 流名称
     * @param segmentDuration 分段时长（秒）
     * @return 是否成功生成MPD文件
     */
    bool generateMPD(const std::string& streamName, float segmentDuration = 4.0f);
    
    /**
     * 检查是否为分段MP4模式
     * 
     * @return 是否为分段MP4模式
     */
    bool isFragmented() const { return m_isFragmented; }

private:
    // 解析NALU数据
    bool parseNALU(const uint8_t* data, size_t size, std::vector<std::pair<const uint8_t*, size_t>>& nalus);
    
    // 处理H264的SPS/PPS
    bool processH264ParameterSets(const uint8_t* sps, size_t spsSize, const uint8_t* pps, size_t ppsSize);
    
    // 处理H265的VPS/SPS/PPS
    bool processH265ParameterSets(const uint8_t* vps, size_t vpsSize, const uint8_t* sps, size_t spsSize, const uint8_t* pps, size_t ppsSize);
    
    // 生成文件名
    std::string generateFileName() const;
    
    /**
     * 自动检测视频编码类型（H264/H265）
     * 
     * @param frameData 帧数据（包含起始码 0x00 0x00 0x00 0x01）
     * @param frameSize 数据大小
     * @return 是否为H265编码
     */
    bool detectCodecType(const uint8_t* frameData, size_t frameSize);

private:
    int m_width;
    int m_height;
    float m_frameRate;
    bool m_isH265;
    bool m_isRecording;
    bool m_hasParameterSets;
    
    std::string m_outputDir;
    std::string m_currentFilePath;
    
    GF_ISOFile* m_mp4File;
    int m_trackId;
    uint64_t m_sampleDuration;
    uint64_t m_currentDTS;
    
    // 参数集配置
    std::unique_ptr<GF_AVCConfig> m_avcConfig;
    std::unique_ptr<GF_HEVCConfig> m_hevcConfig;
    
    // 记录开始时间
    std::chrono::system_clock::time_point m_startTime;
    
    // 分段MP4相关
    bool m_isFragmented;        // 是否为分段MP4模式
    std::string m_dashOutputDir; // DASH输出目录
    int m_fragmentCount;         // 分段计数
    uint32_t m_fragmentDuration;  // 分段时长（毫秒）
};

#endif // H264MP4_WRITER_H