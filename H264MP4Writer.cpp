#include "H264MP4Writer.h"
#include <gpac/internal/isomedia_dev.h>
#include <gpac/constants.h>
#include <gpac/tools.h>
#include <gpac/mpeg4_odf.h>
#include <iostream>
#include <sys/stat.h>
#include <iomanip>
#include <sstream>

H264MP4Writer::H264MP4Writer()
    : m_width(0)
    , m_height(0)
    , m_frameRate(0.0f)
    , m_isH265(false)
    , m_isRecording(false)
    , m_hasParameterSets(false)
    , m_mp4File(nullptr)
    , m_trackId(0)
    , m_sampleDuration(0)
    , m_currentDTS(0)
    , m_isFragmented(false)
    , m_fragmentCount(0)
    , m_fragmentDuration(0)
{
    // 初始化GPAC
    gf_sys_init(GF_MemTrackerNone);
}

H264MP4Writer::~H264MP4Writer()
{
    // 确保停止录制
    if (m_isRecording) {
        stopRecording();
    }
    
    // 清理GPAC
    gf_sys_close();
}

bool H264MP4Writer::init(int width, int height, float frameRate, int isH265)
{
    if (width <= 0 || height <= 0 || frameRate <= 0) {
        std::cerr << "Invalid parameters: width, height and frameRate must be positive" << std::endl;
        return false;
    }
    
    m_width = width;
    m_height = height;
    m_frameRate = frameRate;
    m_isH265 = (isH265 == -1) ? false : (isH265 != 0); // 默认为H264，等待自动检测
    m_hasParameterSets = false;
    
    // 计算采样持续时间 (timescale=1000 表示毫秒)
    m_sampleDuration = static_cast<uint64_t>(90000 / frameRate);
    
    return true;
}

bool H264MP4Writer::startRecording(const std::string& outputDir)
{
    if (m_isRecording) {
        std::cerr << "Already recording" << std::endl;
        return false;
    }
    
    if (m_width <= 0 || m_height <= 0 || m_frameRate <= 0) {
        std::cerr << "Writer not initialized" << std::endl;
        return false;
    }
    
    // 确保输出目录存在
    m_outputDir = outputDir;
    
    // 使用传统方法检查目录是否存在
    // 确保输出目录存在
    struct stat st;
    if (stat(outputDir.c_str(), &st) != 0) {
        // 目录不存在，创建目录
        #ifdef _WIN32
        // Windows平台 - 修正Windows下的目录创建命令
        std::string dirCmd = "mkdir ";
        // 替换所有的正斜杠为反斜杠
        std::string winDir = outputDir;
        for (size_t i = 0; i < winDir.length(); i++) {
            if (winDir[i] == '/') {
                winDir[i] = '\\';
            }
        }
        dirCmd += winDir;
        if (system(dirCmd.c_str()) != 0) {
            std::cerr << "Failed to create output directory: " << outputDir << std::endl;
            return false;
        }
        #else
        // Linux/Unix平台
        if (system(("mkdir -p \"" + outputDir + "\"").c_str()) != 0) {
            std::cerr << "Failed to create output directory: " << outputDir << std::endl;
            return false;
        }
        #endif
    }
    
    // 生成文件名
    m_currentFilePath = outputDir + "/" + generateFileName();
    
    // 创建MP4文件
    m_mp4File = gf_isom_open(m_currentFilePath.c_str(), GF_ISOM_OPEN_WRITE, NULL);
    GF_Err err = m_mp4File ? GF_OK : GF_IO_ERR;
    if (err != GF_OK) {
        std::cerr << "Failed to create MP4 file: " << gf_error_to_string(err) << std::endl;
        return false;
    }
    
    // 添加视频轨道
    m_trackId = gf_isom_new_track(m_mp4File, 0, GF_ISOM_MEDIA_VISUAL, 90000);
    if (!m_trackId) {
        std::cerr << "Failed to create video track" << std::endl;
        gf_isom_delete(m_mp4File);
        m_mp4File = nullptr;
        return false;
    }
    gf_isom_set_track_enabled(m_mp4File, m_trackId, 1);
    
    
    // 设置编解码器类型
    if (m_isH265) {
        // 使用临时空配置，后续会更新
        GF_HEVCConfig *hevc_cfg = gf_odf_hevc_cfg_new();
        u32 trackId = m_trackId;
        err = gf_isom_hevc_config_new(m_mp4File, m_trackId, hevc_cfg, NULL, NULL, &trackId);
        m_trackId = trackId;
        gf_odf_hevc_cfg_del(hevc_cfg);
    } else {
        // 使用临时空配置，后续会更新
        GF_AVCConfig *avc_cfg = gf_odf_avc_cfg_new();
        u32 trackId = m_trackId;
        err = gf_isom_avc_config_new(m_mp4File, m_trackId, avc_cfg, NULL, NULL, &trackId);
        m_trackId = trackId;
        gf_odf_avc_cfg_del(avc_cfg);
    }
    
    if (err != GF_OK) {
        std::cerr << "Failed to set codec config: " << gf_error_to_string(err) << std::endl;
        gf_isom_delete(m_mp4File);
        m_mp4File = nullptr;
        return false;
    }

    
    // 设置视频参数
    err = gf_isom_set_visual_info(m_mp4File, m_trackId, 1, m_width, m_height);
    if (err != GF_OK) {
        std::cerr << "Failed to set visual info: " << gf_error_to_string(err) << std::endl;
        gf_isom_delete(m_mp4File);
        m_mp4File = nullptr;
        return false;
    }

    
    // 记录开始时间
    m_startTime = std::chrono::system_clock::now();
    m_currentDTS = 0;
    m_isRecording = true;
    
    return true;
}

bool H264MP4Writer::stopRecording()
{
    if (!m_isRecording) {
        return false;
    }
    
    GF_Err err = GF_OK;
    
    // 如果是分段MP4，需要特殊处理
    if (m_isFragmented) {
        // 结束当前分段
        if (m_fragmentCount > 0) {
            err = gf_isom_flush_fragments(m_mp4File, GF_TRUE);
            if (err != GF_OK) {
                std::cerr << "Failed to flush fragments: " << gf_error_to_string(err) << std::endl;
            }
        }
        
        // 完成分段MP4文件
        err = gf_isom_close(m_mp4File);
    } else {
        // 普通MP4文件直接关闭
        err = gf_isom_close(m_mp4File);
    }
    
    if (err != GF_OK) {
        std::cerr << "Failed to close MP4 file: " << gf_error_to_string(err) << std::endl;
    }
    
    m_mp4File = nullptr;
    m_isRecording = false;
    m_hasParameterSets = false;
    m_avcConfig.reset();
    m_hevcConfig.reset();
    m_isFragmented = false;
    
    return true;
}

bool H264MP4Writer::writeFrame(const uint8_t* frameData, size_t frameSize, bool isKeyFrame, int64_t timestamp)
{
    if (!m_isRecording || !m_mp4File || !frameData || frameSize == 0) {
        return false;
    }
    
    // 解析NALU
    std::vector<std::pair<const uint8_t*, size_t>> nalus;
    if (!parseNALU(frameData, frameSize, nalus) || nalus.empty()) {
        std::cerr << "Failed to parse NALUs" << std::endl;
        return false;
    }
    
    // 自动检测编码类型（如果需要）
    if (!m_hasParameterSets) {
        // 检查是否需要自动检测
        bool needDetect = false;
        if (!m_hasParameterSets) {
            // 检查第一个NALU的类型来判断是H264还是H265
            bool isH265Detected = detectCodecType(frameData, frameSize);
            
            // 如果检测结果与当前设置不同，更新编码类型
            if (isH265Detected != m_isH265) {
                std::cout << "Auto-detected " << (isH265Detected ? "H265" : "H264") << " codec" << std::endl;
                m_isH265 = isH265Detected;
                
                // 如果已经开始录制，需要重新配置编解码器
                if (m_isRecording) {
                    GF_Err err;
                    if (m_isH265) {
                        // 重新配置为H265编解码器
                        GF_HEVCConfig *hevc_cfg = gf_odf_hevc_cfg_new();
                        u32 trackId = m_trackId;
                        err = gf_isom_hevc_config_new(m_mp4File, m_trackId, hevc_cfg, NULL, NULL, &trackId);
                        m_trackId = trackId;
                        gf_odf_hevc_cfg_del(hevc_cfg);
                    } else {
                        // 重新配置为H264编解码器
                        GF_AVCConfig *avc_cfg = gf_odf_avc_cfg_new();
                        u32 trackId = m_trackId;
                        err = gf_isom_avc_config_new(m_mp4File, m_trackId, avc_cfg, NULL, NULL, &trackId);
                        m_trackId = trackId;
                        gf_odf_avc_cfg_del(avc_cfg);
                    }
                    
                    if (err != GF_OK) {
                        std::cerr << "Failed to set codec config: " << gf_error_to_string(err) << std::endl;
                        return false;
                    }
                }
            }
        }
    }
    
    // 处理参数集
    if (!m_hasParameterSets) {
        if (m_isH265) {
            // 查找VPS, SPS, PPS
            const uint8_t* vps = nullptr;
            size_t vpsSize = 0;
            const uint8_t* sps = nullptr;
            size_t spsSize = 0;
            const uint8_t* pps = nullptr;
            size_t ppsSize = 0;
            
            for (const auto& nalu : nalus) {
                // 获取NALU类型 (H265: (nalu[0] & 0x7E) >> 1)
                
                uint8_t naluType = (nalu.first[0] & 0x7E) >> 1;
                std::cout << "naluType = " << static_cast<int>(naluType) << std::endl;
                if (naluType == 32) { // VPS
                    vps = nalu.first;
                    vpsSize = nalu.second;
                } else if (naluType == 33) { // SPS
                    sps = nalu.first;
                    spsSize = nalu.second;
                } else if (naluType == 34) { // PPS
                    pps = nalu.first;
                    ppsSize = nalu.second;
                }
            }
            
            if (vps && sps && pps) {
                if (!processH265ParameterSets(vps, vpsSize, sps, spsSize, pps, ppsSize)) {
                    std::cerr << "Failed to process H265 parameter sets" << std::endl;
                    return false;
                }
                m_hasParameterSets = true;
            }
        } else {
            // 查找SPS, PPS
            const uint8_t* sps = nullptr;
            size_t spsSize = 0;
            const uint8_t* pps = nullptr;
            size_t ppsSize = 0;
            
            for (const auto& nalu : nalus) {
                // 获取NALU类型 (H264: nalu[0] & 0x1F)
                uint8_t naluType = nalu.first[0] & 0x1F;
                if (naluType == 7) { // SPS
                    sps = nalu.first;
                    spsSize = nalu.second;
                } else if (naluType == 8) { // PPS
                    pps = nalu.first;
                    ppsSize = nalu.second;
                }
            }
            
            if (sps && pps) {
                if (!processH264ParameterSets(sps, spsSize, pps, ppsSize)) {
                    std::cerr << "Failed to process H264 parameter sets" << std::endl;
                    return false;
                }
                m_hasParameterSets = true;
            }
        }
        
        // 如果没有找到参数集，返回等待下一帧
        if (!m_hasParameterSets) {
            return true;
        }
    }
    
    // 准备样本数据
    GF_ISOSample sample;
    memset(&sample, 0, sizeof(GF_ISOSample));
    
    // 计算时间戳
    if (timestamp >= 0) {
        sample.DTS = timestamp;
    } else {
        sample.DTS = m_currentDTS;
        m_currentDTS += m_sampleDuration;
    }
    
    sample.CTS_Offset = 0;
    sample.IsRAP = isKeyFrame ? RAP : RAP_NO;
    
    // 准备样本数据
    size_t totalSize = 0;
    for (const auto& nalu : nalus) {
        // 跳过参数集NALU
        uint8_t naluType;
        if (m_isH265) {
            naluType = (nalu.first[0] & 0x7E) >> 1;
            if (naluType == 32 || naluType == 33 || naluType == 34) { // VPS, SPS, PPS
                continue;
            }
        } else {
            naluType = nalu.first[0] & 0x1F;
            if (naluType == 7 || naluType == 8) { // SPS, PPS
                continue;
            }
        }
        
        totalSize += nalu.second + 4; // 4字节为NALU长度前缀
    }
    
    if (totalSize == 0) {
        // 只有参数集，没有实际数据
        return true;
    }
    
    // 分配样本数据内存
    sample.data = static_cast<char*>(gf_malloc(totalSize));
    if (!sample.data) {
        std::cerr << "Failed to allocate sample data memory" << std::endl;
        return false;
    }
    
    sample.dataLength = totalSize;
    
    // 填充样本数据
    u8* ptr = reinterpret_cast<u8*>(sample.data);
    for (const auto& nalu : nalus) {
        // 跳过参数集NALU
        uint8_t naluType;
        if (m_isH265) {
            naluType = (nalu.first[0] & 0x7E) >> 1;
            if (naluType == 32 || naluType == 33 || naluType == 34) { // VPS, SPS, PPS
                continue;
            }
        } else {
            naluType = nalu.first[0] & 0x1F;
            if (naluType == 7 || naluType == 8) { // SPS, PPS
                continue;
            }
        }
        
        // 写入NALU长度前缀 (4字节大端)
        uint32_t naluSize = nalu.second;
        ptr[0] = (naluSize >> 24) & 0xFF;
        ptr[1] = (naluSize >> 16) & 0xFF;
        ptr[2] = (naluSize >> 8) & 0xFF;
        ptr[3] = naluSize & 0xFF;
        ptr += 4;
        
        // 写入NALU数据
        memcpy(ptr, nalu.first, naluSize);
        ptr += naluSize;
    }
    
    // 添加样本到轨道
    GF_Err err;
    if (m_isFragmented) {
        // 使用分段MP4的添加样本方法
        err = gf_isom_fragment_add_sample(m_mp4File, m_trackId, &sample,
                                         1, // StreamDescriptionIndex
                                         m_sampleDuration, // Duration
                                         0, // PaddingBits
                                         0, // DegradationPriority
                                         GF_FALSE); // redundantCoding
    } else {
        // 使用普通MP4的添加样本方法
        err = gf_isom_add_sample(m_mp4File, m_trackId, 1, &sample);
    }
    
    if (err != GF_OK) {
        std::cerr << "Failed to add sample: " << gf_error_to_string(err) << std::endl;
        gf_free(sample.data);
        return false;
    }
    
    // 释放样本数据内存
    gf_free(sample.data);
    
    return true;
}

std::string H264MP4Writer::getCurrentFilePath() const
{
    return m_currentFilePath;
}

bool H264MP4Writer::initFragmentedMP4(int width, int height, float frameRate, int isH265, const std::string& outputDir)
{
    if (m_isRecording) {
        std::cerr << "Already recording" << std::endl;
        return false;
    }
    
    // 初始化基本参数
    if (!init(width, height, frameRate, isH265)) {
        return false;
    }
    
    // 设置分段MP4标志
    m_isFragmented = true;
    m_fragmentCount = 0;
    m_dashOutputDir = outputDir;
    
    // 确保输出目录存在
    struct stat st;
    if (stat(outputDir.c_str(), &st) != 0) {
        // 目录不存在，创建目录
        #ifdef _WIN32
        // Windows平台
        if (system(("mkdir \"" + outputDir + "\"").c_str()) != 0) {
            std::cerr << "Failed to create output directory: " << outputDir << std::endl;
            return false;
        }
        #else
        // Linux/Unix平台
        if (system(("mkdir -p \"" + outputDir + "\"").c_str()) != 0) {
            std::cerr << "Failed to create output directory: " << outputDir << std::endl;
            return false;
        }
        #endif
    }
    
    // 生成文件名
    m_currentFilePath = outputDir + "/" + generateFileName();
    
    // 创建分段MP4文件
    m_mp4File = gf_isom_open(m_currentFilePath.c_str(), GF_ISOM_OPEN_WRITE, NULL);
    GF_Err err = m_mp4File ? GF_OK : GF_IO_ERR;
    if (err != GF_OK) {
        std::cerr << "Failed to create fragmented MP4 file: " << gf_error_to_string(err) << std::endl;
        return false;
    }
    
    // 设置为分段模式
    // 注意：gf_isom_set_fragmented函数在当前GPAC版本中不存在
    // 我们使用GF_ISOM_WRITE_EDIT标志来创建可编辑的MP4文件，这是分段MP4所必需的
    // GF_ISOM_OPEN_WRITE和GF_ISOM_WRITE_EDIT是互斥的文件打开模式，不应该通过位运算组合使用
    /*err = gf_isom_set_fragmented(m_mp4File, 1);*/
    /*if (err != GF_OK) {
        std::cerr << "Failed to set fragmented mode: " << gf_error_to_string(err) << std::endl;
        gf_isom_delete(m_mp4File);
        m_mp4File = nullptr;
        return false;
    }*/
    
    // 添加视频轨道
    m_trackId = gf_isom_new_track(m_mp4File, 0, GF_ISOM_MEDIA_VISUAL, 90000);
    if (!m_trackId) {
        std::cerr << "Failed to create video track" << std::endl;
        gf_isom_delete(m_mp4File);
        m_mp4File = nullptr;
        return false;
    }
    gf_isom_set_track_enabled(m_mp4File, m_trackId, 1);
    
    // 设置编解码器类型
    if (m_isH265) {
        // 使用临时空配置，后续会更新
        GF_HEVCConfig *hevc_cfg = gf_odf_hevc_cfg_new();
        u32 trackId = m_trackId;
        err = gf_isom_hevc_config_new(m_mp4File, m_trackId, hevc_cfg, NULL, NULL, &trackId);
        m_trackId = trackId;
        gf_odf_hevc_cfg_del(hevc_cfg);
    } else {
        // 使用临时空配置，后续会更新
        GF_AVCConfig *avc_cfg = gf_odf_avc_cfg_new();
        u32 trackId = m_trackId;
        err = gf_isom_avc_config_new(m_mp4File, m_trackId, avc_cfg, NULL, NULL, &trackId);
        m_trackId = trackId;
        gf_odf_avc_cfg_del(avc_cfg);
    }
    
    if (err != GF_OK) {
        std::cerr << "Failed to set codec config: " << gf_error_to_string(err) << std::endl;
        gf_isom_delete(m_mp4File);
        m_mp4File = nullptr;
        return false;
    }

    // 设置视频参数
    err = gf_isom_set_visual_info(m_mp4File, m_trackId, 1, m_width, m_height);
    if (err != GF_OK) {
        std::cerr << "Failed to set visual info: " << gf_error_to_string(err) << std::endl;
        gf_isom_delete(m_mp4File);
        m_mp4File = nullptr;
        return false;
    }
    gf_isom_set_sync_table(m_mp4File, m_trackId);
    if (err != GF_OK) {
        std::cerr << "Failed gf_isom_set_sync_table: " << gf_error_to_string(err) << std::endl;
        gf_isom_delete(m_mp4File);
        m_mp4File = nullptr;
        return false;
    }
    // 准备分段 - 使用正确的参数数量
    err = gf_isom_setup_track_fragment(m_mp4File, m_trackId, 
                                      1,  // DefaultStreamDescriptionIndex
                                      1,  // DefaultSampleDuration
                                      0,  // DefaultSampleSize
                                      0,  // DefaultSampleIsSync
                                      0,  // DefaultSamplePadding
                                      0); // DefaultDegradationPriority
    if (err != GF_OK) {
        std::cerr << "Failed to setup track fragment: " << gf_error_to_string(err) << std::endl;
        gf_isom_delete(m_mp4File);
        m_mp4File = nullptr;
        return false;
    }
    
    // 初始化分段
    err = gf_isom_finalize_for_fragment(m_mp4File, m_trackId);
    if (err != GF_OK) {
        std::cerr << "Failed to finalize for fragment: " << gf_error_to_string(err) << std::endl;
        gf_isom_delete(m_mp4File);
        m_mp4File = nullptr;
        return false;
    }
    
    err = gf_isom_start_segment(m_mp4File,m_currentFilePath.c_str(), GF_TRUE);
    if (err != GF_OK) {
        std::cerr << "Failed gf_isom_start_segment: " << gf_error_to_string(err) << std::endl;
        return false;
    }

    // 记录开始时间
    m_startTime = std::chrono::system_clock::now();
    m_currentDTS = 0;
    m_isRecording = true;
    
    return true;
}

bool H264MP4Writer::startFragment(uint32_t fragmentDuration)
{
    if (!m_isRecording || !m_isFragmented || !m_mp4File) {
        std::cerr << "Not in fragmented recording mode" << std::endl;
        return false;
    }
    
    m_fragmentDuration = fragmentDuration;
    
    // 开始新的分段
    GF_Err err = gf_isom_start_fragment(m_mp4File, GF_TRUE);
    if (err != GF_OK) {
        std::cerr << "Failed to start fragment: " << gf_error_to_string(err) << std::endl;
        return false;
    }
    
    // 设置分段持续时间
    err = gf_isom_set_fragment_reference_time(m_mp4File, m_trackId, m_currentDTS, 0);
    if (err != GF_OK) {
        std::cerr << "Failed to set fragment reference time: " << gf_error_to_string(err) << std::endl;
        return false;
    }
    
    m_fragmentCount++;
    return true;
}

bool H264MP4Writer::endFragment()
{
    if (!m_isRecording || !m_isFragmented || !m_mp4File) {
        std::cerr << "Not in fragmented recording mode" << std::endl;
        return false;
    }
    
    // 结束当前分段
    GF_Err err = gf_isom_flush_fragments(m_mp4File, GF_TRUE);
    if (err != GF_OK) {
        std::cerr << "Failed to flush fragment: " << gf_error_to_string(err) << std::endl;
        return false;
    }
    
    return true;
}

bool H264MP4Writer::generateMPD(const std::string& streamName, float segmentDuration)
{

    
    // 确保已经停止录制
    if (m_isRecording) {
        stopRecording();
    }
    
    // 创建流目录
    std::string streamDir = m_dashOutputDir + "/" + streamName;
    struct stat st;
    if (stat(streamDir.c_str(), &st) != 0) {
        #ifdef _WIN32
        system(("mkdir \"" + streamDir + "\" 2>nul").c_str());
        #else
        system(("mkdir -p \"" + streamDir + "\"").c_str());
        #endif
    }
    
    // 使用GPAC的MP4Box工具进行DASH分段
    std::string cmd = "MP4Box -dash " + std::to_string((int)(segmentDuration * 1000)) +
                     " -frag " + std::to_string((int)(segmentDuration * 1000)) +
                     " -rap -segment-name segment_ -out \"" + streamDir + "/manifest.mpd\" \"" +
                     m_currentFilePath + "\"";
    
    std::cout << "Executing command: " << cmd << std::endl;
    int result = system(cmd.c_str());
    if (result != 0) {
        std::cerr << "Failed to segment MP4 file: " << m_currentFilePath << std::endl;
        return false;
    }
    
    return true;
}

bool H264MP4Writer::parseNALU(const uint8_t* data, size_t size, std::vector<std::pair<const uint8_t*, size_t>>& nalus)
{
    if (!data || size < 4) {
        return false;
    }
    
    nalus.clear();
    
    // 查找起始码并解析NALU
    const uint8_t* start = nullptr;
    size_t i = 0;
    
    // 查找第一个起始码
    while (i + 3 < size) {
        // 检查起始码 (0x00 0x00 0x00 0x01 或 0x00 0x00 0x01)
        if ((data[i] == 0 && data[i+1] == 0 && data[i+2] == 0 && data[i+3] == 1) ||
            (data[i] == 0 && data[i+1] == 0 && data[i+2] == 1)) {
            
            // 确定起始码长度
            int startCodeLen = (data[i+2] == 0 && data[i+3] == 1) ? 4 : 3;
            
            // 如果已经找到了一个NALU，添加到列表
            if (start) {
                nalus.push_back(std::make_pair(start, data + i - start));
            }
            
            // 更新起始位置为当前NALU的开始
            start = data + i + startCodeLen;
            i += startCodeLen;
        } else {
            i++;
        }
    }
    
    // 添加最后一个NALU
    if (start && start < data + size) {
        nalus.push_back(std::make_pair(start, data + size - start));
    }
    
    return !nalus.empty();
}

bool H264MP4Writer::processH264ParameterSets(const uint8_t* sps, size_t spsSize, const uint8_t* pps, size_t ppsSize)
{
    if (!m_mp4File || !sps || spsSize == 0 || !pps || ppsSize == 0) {
        return false;
    }
    
    // 创建AVC配置
    m_avcConfig = std::unique_ptr<GF_AVCConfig>(gf_odf_avc_cfg_new());
    if (!m_avcConfig) {
        return false;
    }
    
    // 添加SPS
    GF_AVCConfigSlot* spsSlot = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
    if (!spsSlot) {
        return false;
    }
    
    spsSlot->size = spsSize;
    spsSlot->data = static_cast<char*>(gf_malloc(spsSize));
    if (!spsSlot->data) {
        gf_free(spsSlot);
        return false;
    }
    
    memcpy(spsSlot->data, sps, spsSize);
    gf_list_add(m_avcConfig->sequenceParameterSets, spsSlot);
    
    // 添加PPS
    GF_AVCConfigSlot* ppsSlot = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
    if (!ppsSlot) {
        return false;
    }
    
    ppsSlot->size = ppsSize;
    ppsSlot->data = static_cast<char*>(gf_malloc(ppsSize));
    if (!ppsSlot->data) {
        gf_free(ppsSlot);
        return false;
    }
    
    memcpy(ppsSlot->data, pps, ppsSize);
    gf_list_add(m_avcConfig->pictureParameterSets, ppsSlot);
    
    // 设置AVC配置参数
    m_avcConfig->AVCProfileIndication = sps[1];
    m_avcConfig->profile_compatibility = sps[2];
    m_avcConfig->AVCLevelIndication = sps[3];
    m_avcConfig->chroma_format = 1; // 4:2:0
    m_avcConfig->luma_bit_depth = 8;
    m_avcConfig->chroma_bit_depth = 8;
    
    // 更新MP4文件的AVC配置
    u32 flags = m_mp4File->FragmentsFlags;
    m_mp4File->FragmentsFlags = 0;
    GF_Err err = gf_isom_avc_config_update(m_mp4File, m_trackId, 1, m_avcConfig.get());
    m_mp4File->FragmentsFlags = flags;
    if (err != GF_OK) {
        std::cerr << "Failed to update AVC config: " << gf_error_to_string(err) << std::endl;
        return false;
    }
    
    return true;
}

bool H264MP4Writer::processH265ParameterSets(const uint8_t* vps, size_t vpsSize, const uint8_t* sps, size_t spsSize, const uint8_t* pps, size_t ppsSize)
{
    if (!m_mp4File || !vps || vpsSize == 0 || !sps || spsSize == 0 || !pps || ppsSize == 0) {
        return false;
    }
    
    // 创建HEVC配置
    m_hevcConfig = std::unique_ptr<GF_HEVCConfig>(gf_odf_hevc_cfg_new());
    if (!m_hevcConfig) {
        return false;
    }
    
    // 添加VPS
    GF_HEVCParamArray* vpsArray = (GF_HEVCParamArray*)gf_malloc(sizeof(GF_HEVCParamArray));
    if (!vpsArray) {
        return false;
    }
    
    vpsArray->array_completeness = 1;
    vpsArray->type = GF_HEVC_NALU_VID_PARAM;
    vpsArray->nalus = gf_list_new();
    
    GF_AVCConfigSlot* vpsSlot = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
    if (!vpsSlot) {
        gf_free(vpsArray);
        return false;
    }
    
    vpsSlot->size = vpsSize;
    vpsSlot->data = static_cast<char*>(gf_malloc(vpsSize));
    if (!vpsSlot->data) {
        gf_free(vpsSlot);
        gf_free(vpsArray);
        return false;
    }
    
    memcpy(vpsSlot->data, vps, vpsSize);
    gf_list_add(vpsArray->nalus, vpsSlot);
    gf_list_add(m_hevcConfig->param_array, vpsArray);
    
    // 添加SPS
    GF_HEVCParamArray* spsArray = (GF_HEVCParamArray*)gf_malloc(sizeof(GF_HEVCParamArray));
    if (!spsArray) {
        return false;
    }
    
    spsArray->array_completeness = 1;
    spsArray->type = GF_HEVC_NALU_SEQ_PARAM;
    spsArray->nalus = gf_list_new();
    
    GF_AVCConfigSlot* spsSlot = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
    if (!spsSlot) {
        gf_free(spsArray);
        return false;
    }
    
    spsSlot->size = spsSize;
    spsSlot->data = static_cast<char*>(gf_malloc(spsSize));
    if (!spsSlot->data) {
        gf_free(spsSlot);
        gf_free(spsArray);
        return false;
    }
    
    memcpy(spsSlot->data, sps, spsSize);
    gf_list_add(spsArray->nalus, spsSlot);
    gf_list_add(m_hevcConfig->param_array, spsArray);
    
    // 添加PPS
    GF_HEVCParamArray* ppsArray = (GF_HEVCParamArray*)gf_malloc(sizeof(GF_HEVCParamArray));
    if (!ppsArray) {
        return false;
    }
    
    ppsArray->array_completeness = 1;
    ppsArray->type = GF_HEVC_NALU_PIC_PARAM;
    ppsArray->nalus = gf_list_new();
    
    GF_AVCConfigSlot* ppsSlot = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
    if (!ppsSlot) {
        gf_free(ppsArray);
        return false;
    }
    
    ppsSlot->size = ppsSize;
    ppsSlot->data = static_cast<char*>(gf_malloc(ppsSize));
    if (!ppsSlot->data) {
        gf_free(ppsSlot);
        gf_free(ppsArray);
        return false;
    }
    
    memcpy(ppsSlot->data, pps, ppsSize);
    gf_list_add(ppsArray->nalus, ppsSlot);
    gf_list_add(m_hevcConfig->param_array, ppsArray);
    
    // 设置HEVC配置参数
    m_hevcConfig->configurationVersion = 1;
    m_hevcConfig->profile_space = 0;
    m_hevcConfig->tier_flag = 0;
    m_hevcConfig->profile_idc = 1; // Main profile
    m_hevcConfig->general_profile_compatibility_flags = 0xffffffff;
    m_hevcConfig->progressive_source_flag = 1;
    m_hevcConfig->interlaced_source_flag = 0;
    m_hevcConfig->non_packed_constraint_flag = 0;
    m_hevcConfig->frame_only_constraint_flag = 0;
    m_hevcConfig->level_idc = 51; // Level 5.1
    
    // 更新MP4文件的HEVC配置
    u32 flags = m_mp4File->FragmentsFlags;
    m_mp4File->FragmentsFlags = 0;
    GF_Err err = gf_isom_hevc_config_update(m_mp4File, m_trackId, 1, m_hevcConfig.get());
    m_mp4File->FragmentsFlags = flags;
    if (err != GF_OK) {
        std::cerr << "Failed to update HEVC config: " << gf_error_to_string(err) << std::endl;
        return false;
    }
    
    return true;
}

std::string H264MP4Writer::generateFileName() const
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S.mp4");
    
    return oss.str();
}

bool H264MP4Writer::detectCodecType(const uint8_t* frameData, size_t frameSize)
{
    if (!frameData || frameSize < 4) {
        return false; // 默认为H264
    }
    
    // 解析NALU
    std::vector<std::pair<const uint8_t*, size_t>> nalus;
    if (!parseNALU(frameData, frameSize, nalus) || nalus.empty()) {
        return false; // 默认为H264
    }
    
    // 检查是否有VPS (只有H265有VPS)
    for (const auto& nalu : nalus) {
        uint8_t naluType = (nalu.first[0] & 0x7E) >> 1;
        if (naluType == 32) { // VPS
            return true; // 是H265
        }
    }
    
    return false; // 是H264
}