#ifndef DASH_SERVER_H
#define DASH_SERVER_H

#include <gpac/isomedia.h>
#include <gpac/dash.h>
#include <gpac/internal/mpd.h>
#include <gpac/network.h>
#include <gpac/tools.h>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>
#include <iostream>
// DASH服务器类
class DashServer {
public:
    // 构造函数
    DashServer();
    
    // 析构函数
    ~DashServer();

    // 初始化服务器
    bool init(uint16_t port = 8080, float segmentDuration = 4.0f, const std::string& outputDir = "./dash");

    // 添加MP4文件
    bool addMP4File(const std::string& mp4FilePath, const std::string& streamName);

    // 启动服务器
    bool start();

    // 停止服务器
    void stop();

private:
    // 服务器主循环
    void serverLoop();

    // 处理客户端请求
    void handleClient(int clientSocket);

    // 处理根路径请求
    void handleRootRequest(int clientSocket);

    // 处理MPD请求
    void handleMPDRequest(int clientSocket, const std::string& path);

    // 处理分段请求
    void handleSegmentRequest(int clientSocket, const std::string& path);

    // 发送HTTP响应
    void sendResponse(int clientSocket, const char* status, const char* contentType, const std::string& content);

private:
    std::atomic<bool> m_running;       // 服务器运行状态
    std::thread m_serverThread;        // 服务器线程
    int m_serverSocket;                // 服务器套接字
    uint16_t m_port;                   // 服务器端口
    float m_segmentDuration;           // 分段时长（秒）
    std::string m_outputDir;           // 输出目录
    std::map<std::string, std::string> m_streams;  // 流列表 <流名称, MP4文件路径>
    std::mutex m_streamsMutex;         // 流列表互斥锁
};

#endif // DASH_SERVER_H