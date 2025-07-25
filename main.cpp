#include "H264MP4Writer.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <chrono>
#include <sys/stat.h>  // 包含必要的头文件
#include <unistd.h>
#include <sys/time.h>

#define ILOGE printf
#define ILOGW printf
#define ILOGD printf

#define APP_SYS_AV_AUDIO_FRAME_SIZE (640) ////音频一帧最大长度640，开发者根据自身的硬件来确定 16000*2/25=1280  8000*2/25=640 ak帧长:512(pcm),256(g711u)
#define APP_SYS_AV_VIDEO_FRAME_SIZE_100K (100 * 1024) //标清子码流最大100k

#define DHAV_HEAD_LENGTH                    (24)
#define DHAV_TAIL_LENGTH                    (8)
#define DHAV_EXTRA_OFFSET                   (22)
#define DHAV_CHECK_SUM_INDEX                (23)
#define VIDEO_MAIN_STREAM_QUEUE_SIZE        (32)
// #define h264

///帧类型
typedef enum
{
	I_FRAME_FLAG        = 0xFD,	///< I
	P_FRAME_FLAG        = 0xFC,	///< P
	B_FRAME_FLAG        = 0xFE,	///< B
	JPEG_FRAME_FLAG     = 0xFB,	///< JPEG
	AUDIO_FRAME_FLAG    = 0xF0, ///< AUDIO
	ASSISTANT_FLAG      = 0xF1,	///< 辅助帧,例如水印智能分析信息等
}DAHUA_FRAME_TYPE;

// 日期时间
typedef struct
{
	unsigned int second : 6;        //	秒	0-59
	unsigned int minute : 6;        //	分	0-59
	unsigned int hour   : 5;        //	时	0-23
	unsigned int day    : 5;        //	日	1-31
	unsigned int month  : 4;        //	月	1-12
	unsigned int year   : 6;        //	年	2000-2063
}DateTime;

// 大华标准帧头
typedef struct
{
	unsigned char frame_head_flag[4];	///>'D' 'H' 'A' 'V'
	unsigned char type;				    ///>帧类型，详见DAHUA_FRAME_TYPE定义
	unsigned char sub_type;			    ///>子类型，辅助帧才用到 0x01–调试信息,0x02-自定义信息....

	unsigned char channel_id;			///>通道号 通道表示回放需要的所有数据.每个通道可以包含1个视频+多个音频+多个辅助数据, 
                                        ///>如果他们的数据在同一个流中,他们的通道号必须填成一样,这样回放程序才能识别他们.
                                        //>通道号是一个相对数值,仅用于区分同一个流中的不同通道.

	unsigned char sub_frame_indx;		///>子帧序号 超长视频帧可以分成多个封装子帧，帧序号不变，子帧序号从大逐步递减到0
                                        //> 正常帧的子帧序号为0

	unsigned int frame_indx;			///>帧序号
	unsigned int frame_len;			    ///>帧长度，帧头+数据长度+帧尾
	DateTime  time;				        ///>时间日期
	unsigned short time_ms;			    ///>绝对时间戳
	unsigned char expand_len;			///>扩展字段长度
	unsigned char verify;				///>校验和，前23字节累加和
}__attribute__((packed)) DAHUA_FRAME_HEAD;


///扩展帧帧头标志位,参照《大华标准码流格式定义.pptx》svn@561961
typedef enum
{
    IMAGE_TYPE_FLAG				=	0x80,	///> 图像尺寸-1字段，4字节
    PLAY_BACK_TYPE_FLAG			=	0x81,	///> 回放类型字段，4字节
    IMAGE_H_TYPE_FLAG			=	0x82,   ///> 图像尺寸-2字段，8字节
    AUDIO_TYPE_FLAG				=	0x83,	///> 音频格式字段，4字节
    IVS_EXPAND_FLAG				=	0x84,	///> 智能扩展字段，8字节
    MODIFY_EXPAND_FLAG			=	0x85, 	///> 修定扩展字段，4字节
    DATA_VERIFY_DATA_FLAG		=	0x88,	///> 数据校验字段，8字节
    DATA_ENCRYPT_FLAG			=	0x89,	///> 数据加密字段，4字节
    FRACTION_FRAMERATE_FLAG 	=   0x8a,   ///> 扩展回放类型分数帧率字段，8字节
    STREAM_ROTATION_ANGLE_FLAG 	=   0x8b,   ///> 码流旋转角度字段，4字节
    AUDIO_TYPE_FLAG_EX			=	0x8c,	///> 扩展音频格式字段，指定长度

    METADATA_EXPAND_LEN_FLAG	=	0x90,	///> 元数据子帧长度扩展字段，8字节
    IMAGE_IMPROVEMENT_FLAG  	=   0x91,	///> 图像优化字段，8字节
    STREAM_MANUFACTURER_FLAG	=   0x92,	///> 码流厂商类型字段，8字节
    PENETRATE_FOG_FLAG      	=   0x93,	///> 偷雾模式标志字段，8字节
    SVC_FLAG					=	0x94,	///> SVC-T可伸缩视频编解码字段，4字节
    FRAME_ENCRYPT_FLAG			=	0x95,	///> 帧加密标志字段，8字节
    AUDIO_CHANNEL_FLAG			=	0x96,   ///> 音频通道扩展帧头标识字段，4字节
    PICTURE_REFORMATION_FLAG	=	0x97, 	///> 图像重组字段, 8+n*16字节
    DATA_ALIGNMENT_FLAG			=	0x98, 	///> 数据对齐字段，4字节
    IMAGE_MOSAIC_FLAG			=	0x99,   ///> 图像拼接扩展字段, 8+n*m*16字节
    FISH_EYE_FLAG           	=   0x9a,   ///> 鱼眼功能字段，8字节
    IMAGE_WH_RATIO_FLAG     	=   0x9b,   ///> 视频宽高比字段，8字节
    DIGITAL_SIGNATRUE_FLAG		=	0x9c,	///> 数字签名字段, 特殊处理

    ABSOLUTE_MILLISED_FLAG		=	0xa0,	///>  绝对毫秒时间字段
    NET_TRANSPORT_FLAG			=	0xa1,	///>  网络传输标识字段

    VEDIO_ENCRYPT_FRAME			=	0xb0, 	///> 录像加密帧字段
    OSD_STRING_FLAG         	=   0xb1,   ///> 码流OSD 字段
    GOP_OFFSET_FLAG         	=   0xb2,   ///> 解码偏移参考字段
    ENCYPT_CHECK_FLAG			=	0xb3,	///> 加密密钥校验字段
    SENSOR_JOIN_FLAG        	=   0xb4,   ///> 多目相机SENSOR 拼接字段
    STREAM_ENCRYPT_FLAG			=	0xb5,	///> 码流加密字段

    EXTERNHEAD_FLAG_RESERVED	=   0xff,	///> 大华扩展帧类型0xFF保留字段
}DAHUA_EXTERNHEAD_FLAG;

typedef struct
{
    uint8_t     type;               ///< 扩展帧标志 DAHUA_EXTERNHEAD_FLAG
    uint8_t 	encode;			    ///< 编码 0:编码时只有一场(帧) 1:编码时两场交织 2:编码时分两场
    uint8_t 	width;			    ///< 宽(8像素点为1单位)
    uint8_t     height;             ///< 高(8像素点为1单位)
}__attribute__((packed)) FRAME_EXTEND_IMAGE_SIZE1;

///视频编码格式
typedef enum
{
	MPEG4 = 1,
	H264 = 2,
	MPEG4_LB = 3,
	H264_GBE = 4,
	JPEG = 5,
	JPEG2000 = 6,
	AVS = 7,
	MPEG2= 9,
	VNC = 10,
	SVAC = 11,
	H265 = 12
}DAHUA_VIDEO_ENCODE_TYPE;

typedef struct
{
    uint8_t     type;               ///< 扩展帧标志 DAHUA_EXTERNHEAD_FLAG
    uint8_t 	interval;			///< I帧间隔(每多少帧一个I帧),取值范围1～255, 0表示老版本的码流
    uint8_t 	protocal;			///< 协议类型 见 DAHUA_VIDEO_ENCODE_TYPE
    uint8_t     fps;                ///< 帧率
}__attribute__((packed)) FRAME_EXTEND_PLAYBACK;

// 扩展帧头 - 数据校验
typedef struct
{
    unsigned char type;				    //0X88 表示校验信息
    unsigned char verify_result[4];     //校验结果
    unsigned char verify_method;		//保留，暂时没用
    unsigned char reserved2;		    //保留，暂时没用
    unsigned char verify_type;		    //校验类型， 目前为2： CRC32
} __attribute__((packed)) DATA_VERIFY;

// 大华帧尾
typedef struct
{
	unsigned char frame_tail_flag[4];	///>'D' 'H' 'A' 'V'
	unsigned int data_len;				///>数据长度，帧头+数据长度+帧尾
}__attribute__((packed)) DAHUA_FRAME_TAIL;


typedef struct
{
    int32_t frametype;
    int32_t usedSize;
    int64_t pts;
    unsigned char* frameBuff;
}VideoMsg;
void processVideoFile(H264MP4Writer& writer);
static int32_t read_video_file(const char* path, char** fileBuf, int32_t* fileLen)
{
    if (path == NULL || fileBuf == NULL || fileLen == NULL)
    {
        ILOGW("[%s] input param err", __func__);
        return -1;
    }
    
    // 使用Linux stat检查文件是否存在
    struct stat st;
    if (stat(path, &st) != 0)
    {
        ILOGW("%s not exist\n", path);
        return -1;
    }

    FILE* file = fopen(path, "rb");
    if (file == NULL)
    {
        ILOGE("[%s] fopen err: %s", __func__, strerror(errno));
        return -1;
    }

    // 获取文件大小
    fseek(file, 0, SEEK_END);
    int32_t fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    ILOGD("[%s] file size = %d", __func__, fileSize);

    char* pTmpBuf = (char*)malloc(fileSize);
    if (pTmpBuf == NULL)
    {
        ILOGE("[%s] malloc err", __func__);
        fclose(file);
        return -1;
    }

    size_t readSize = fread(pTmpBuf, 1, fileSize, file);
    if (readSize != fileSize)
    {
        ILOGE("[%s] fread err: %s", __func__, strerror(errno));
        fclose(file);
        free(pTmpBuf);
        return -1;
    }

    *fileBuf = pTmpBuf;
    *fileLen = fileSize;
    fclose(file);
    return 0;
}

static int32_t dahua_head_check_sum(char* head, uint8_t sum)
{
    uint8_t calSum = 0;
    for (int32_t i = 0; i < 23; i++)
    {
        calSum += head[i];
    }
    if (calSum != sum)
    {
        ILOGE("[%s] src[0x%x] dst[0x%x]", __func__, sum, calSum);
        return false;
    }
    return true;
}

// 获取ms 时间
static unsigned long __get_time_ms()
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0) {
        return 0;
    }
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}


// 模拟生成H264帧数据的函数
void generateDummyH264Frame(std::vector<uint8_t>& frameData, bool isKeyFrame, int frameIndex) {
    // 起始码
    frameData.push_back(0x00);
    frameData.push_back(0x00);
    frameData.push_back(0x00);
    frameData.push_back(0x01);
    
    if (frameIndex == 0) {
        // 第一帧，添加SPS
        frameData.push_back(0x67); // SPS NALU类型
        // 添加一些模拟的SPS数据
        for (int i = 0; i < 20; i++) {
            frameData.push_back(0x10 + i);
        }
        
        // 添加PPS
        frameData.push_back(0x00);
        frameData.push_back(0x00);
        frameData.push_back(0x00);
        frameData.push_back(0x01);
        frameData.push_back(0x68); // PPS NALU类型
        // 添加一些模拟的PPS数据
        for (int i = 0; i < 10; i++) {
            frameData.push_back(0x20 + i);
        }
    }
    
    // 添加帧数据起始码
    frameData.push_back(0x00);
    frameData.push_back(0x00);
    frameData.push_back(0x00);
    frameData.push_back(0x01);
    
    if (isKeyFrame) {
        frameData.push_back(0x65); // IDR帧 NALU类型
    } else {
        frameData.push_back(0x41); // P帧 NALU类型
    }
    
    // 添加一些模拟的帧数据
    for (int i = 0; i < 100; i++) {
        frameData.push_back(static_cast<uint8_t>(i + frameIndex));
    }
}

// 普通MP4录制演示函数
void normalMP4Demo() {
    std::cout << "=== 普通MP4录制演示 ===" << std::endl;
    
    // 创建H264MP4Writer实例
    H264MP4Writer writer;
    
    // 初始化参数 (宽度, 高度, 帧率)
    if (!writer.init(1920, 1080, 25, true)) {
        std::cerr << "Failed to initialize writer" << std::endl;
        return;
    }
    
    // 开始录制，文件将保存在指定目录
    if (!writer.startRecording("./videos")) {
        std::cerr << "Failed to start recording" << std::endl;
        return;
    }
    
    std::cout << "Recording started. Output file: " << writer.getCurrentFilePath() << std::endl;
    std::cout << "模拟写入H264帧数据..." << std::endl;
    
    // 读取并处理视频文件
    processVideoFile(writer);
    
    // 停止录制
    writer.stopRecording();
    std::cout << "Recording stopped" << std::endl;
    std::cout << "MP4 file saved to: " << writer.getCurrentFilePath() << std::endl;
}

// fMP4(分段MP4)录制演示函数
void fragmentedMP4Demo() {
    std::cout << "\n=== 分段MP4(fMP4)录制演示 ===" << std::endl;
    
    // 创建H264MP4Writer实例
    H264MP4Writer writer;
    
    // 初始化分段MP4 (宽度, 高度, 帧率, 是否H265, 输出目录)
    if (!writer.initFragmentedMP4(1920, 1080, 25, true, "./dash")) {
        std::cerr << "Failed to initialize fragmented MP4 writer" << std::endl;
        return;
    }
    
    std::cout << "Fragmented MP4 recording started. Output file: " << writer.getCurrentFilePath() << std::endl;
    
    // 设置分段参数 - 每个分段2秒
    const uint32_t fragmentDuration = 2000; // 2秒，单位毫秒
    const int framesPerFragment = 50; // 25fps * 2秒 = 50帧
    int fragmentCount = 0;
    
    // 开始第一个分段
    if (!writer.startFragment(fragmentDuration)) {
        std::cerr << "Failed to start first fragment" << std::endl;
        return;
    }
    fragmentCount++;
    std::cout << "Started fragment #" << fragmentCount << std::endl;
    
    // 读取并处理视频文件，每50帧创建一个新分段
    char* fileBuf = NULL;
    int32_t fileLen = 0;
    char path[] = "./v_demo.dav";
    int32_t ret = read_video_file(path, &fileBuf, &fileLen);
    if (ret) {
        std::cerr << "Failed to read video file" << std::endl;
        return;
    }
    
    char* pTmpHead = fileBuf;
    int frameCounter = 0;
    
    while (1) {
        if (!(pTmpHead[0] == 'D' && pTmpHead[1] == 'H' && pTmpHead[2] == 'A' && pTmpHead[3] == 'V')) {
            break;
        }
        
        DAHUA_FRAME_HEAD* head = (DAHUA_FRAME_HEAD*)pTmpHead;
        
        if (dahua_head_check_sum((char*)head, head->verify) == false) {
            break;
        }
        
        int32_t data_length = head->frame_len - DHAV_HEAD_LENGTH - DHAV_TAIL_LENGTH - head->expand_len;
        int32_t data_offset = DHAV_HEAD_LENGTH + head->expand_len;
        
        VideoMsg msg = {0};
        msg.frametype = head->type == I_FRAME_FLAG;
        msg.usedSize = data_length;
        msg.pts = __get_time_ms();
        msg.frameBuff = (unsigned char*)(pTmpHead + data_offset);
        
        // 写入帧数据
        if (!writer.writeFrame(msg.frameBuff, msg.usedSize, msg.frametype)) {
            std::cerr << "Failed to write frame" << std::endl;
            continue;
        }
        
        frameCounter++;
        
        // 每framesPerFragment帧结束当前分段并开始新分段
        if (frameCounter % framesPerFragment == 0) {
            // 结束当前分段
            if (!writer.endFragment()) {
                std::cerr << "Failed to end fragment #" << fragmentCount << std::endl;
            } else {
                std::cout << "Ended fragment #" << fragmentCount << std::endl;
            }
            
            // 开始新分段
            if (!writer.startFragment(fragmentDuration)) {
                std::cerr << "Failed to start fragment #" << (fragmentCount + 1) << std::endl;
            } else {
                fragmentCount++;
                std::cout << "Started fragment #" << fragmentCount << std::endl;
            }
        }
        
        pTmpHead += head->frame_len;
        
        // 限制最多处理5个分段的数据
        // if (fragmentCount >= 5 && frameCounter % framesPerFragment == 0) {
        //     break;
        // }
    }
    
    // 结束最后一个分段
    if (!writer.endFragment()) {
        std::cerr << "Failed to end last fragment" << std::endl;
    } else {
        std::cout << "Ended last fragment" << std::endl;
    }
    
    // 释放资源
    if (fileBuf) {
        free(fileBuf);
        fileBuf = NULL;
    }
    
    // 停止录制
    writer.stopRecording();
    std::cout << "Fragmented MP4 recording stopped" << std::endl;
    
    // 生成DASH MPD文件
    if (!writer.generateMPD("video", 2.0f)) {
        std::cerr << "Failed to generate MPD file" << std::endl;
    } else {
        std::cout << "Generated MPD file at: ./dash/video/manifest.mpd" << std::endl;
        std::cout << "You can now play this stream using a DASH player" << std::endl;
    }
}

// 处理视频文件的函数
void processVideoFile(H264MP4Writer& writer) {
    char* fileBuf = NULL;
    int32_t fileLen = 0;
    char path[] = "./v_demo.dav";
    int32_t ret = read_video_file(path, &fileBuf, &fileLen);
    if (ret) {
        ILOGE("[%s] read_video_file err", __func__);
        usleep(1000 * 1000);
        return;
    }
    
    char* pTmpHead = fileBuf;
    
    while (1) {
        if (!(pTmpHead[0] == 'D' && pTmpHead[1] == 'H' && pTmpHead[2] == 'A' && pTmpHead[3] == 'V')) {
            ILOGE("[%s] invalid frame", __func__);
            break;
        }
        
        DAHUA_FRAME_HEAD* head = (DAHUA_FRAME_HEAD*)pTmpHead;
        
        if (dahua_head_check_sum((char*)head, head->verify) == false) {
            ILOGE("[%s] dahua_head_check_sum", __func__);
            break;
        }
        
        int32_t data_length = head->frame_len - DHAV_HEAD_LENGTH - DHAV_TAIL_LENGTH - head->expand_len;
        int32_t data_offset = DHAV_HEAD_LENGTH + head->expand_len;
        
        VideoMsg msg = {0};
        msg.frametype = head->type == I_FRAME_FLAG;
        msg.usedSize = data_length;
        msg.pts = __get_time_ms();
        msg.frameBuff = (unsigned char*)(pTmpHead + data_offset);
        
        // 写入帧数据
        if (!writer.writeFrame(msg.frameBuff, msg.usedSize, msg.frametype)) {
            std::cerr << "Failed to write frame" << std::endl;
            continue;
        }
        
        pTmpHead += head->frame_len;
    }
    
    if (fileBuf) {
        free(fileBuf);
        fileBuf = NULL;
    }
}

int main() {
    std::cout << "H264MP4Writer Demo" << std::endl;
    
    // 选择演示模式
    int choice = 0;
    std::cout << "请选择演示模式:\n";
    std::cout << "1. 普通MP4录制\n";
    std::cout << "2. 分段MP4(fMP4)录制 (用于DASH流媒体)\n";
    std::cout << "3. 两种模式都演示\n";
    std::cout << "请输入选择 (1-3): ";
    std::cin >> choice;
    
    switch (choice) {
        case 1:
            normalMP4Demo();
            break;
        case 2:
            fragmentedMP4Demo();
            break;
        case 3:
            normalMP4Demo();
            fragmentedMP4Demo();
            break;
        default:
            std::cout << "无效选择，默认演示普通MP4录制" << std::endl;
            normalMP4Demo();
            break;
    }
    
    std::cout << "\n演示完成!" << std::endl;
    return 0;
}