#include <iostream>
#include <string>
#include <thread>
#include <chrono>

// 包含DashServer头文件
#include "DashServer.h"

int main(int argc, char* argv[]) {
    // 检查命令行参数
    if (argc < 2) {
        std::cout << "用法: " << argv[0] << " <MP4文件路径> [端口号] [流名称]" << std::endl;
        std::cout << "示例: " << argv[0] << " ./videos/test.mp4 8080 video1" << std::endl;
        return 1;
    }

    // 解析命令行参数
    std::string mp4FilePath = argv[1];
    uint16_t port = (argc > 2) ? std::stoi(argv[2]) : 8080;
    std::string streamName = (argc > 3) ? argv[3] : "video";

    std::cout << "=== DASH流媒体服务器示例 ===" << std::endl;
    std::cout << "MP4文件: " << mp4FilePath << std::endl;
    std::cout << "端口号: " << port << std::endl;
    std::cout << "流名称: " << streamName << std::endl;

    // 创建DASH服务器
    DashServer server;

    // 初始化服务器
    std::cout << "\n[1] 初始化服务器..." << std::endl;
    if (!server.init(port, 4.0f, "./dash")) {
        std::cerr << "初始化DASH服务器失败" << std::endl;
        return 1;
    }
    std::cout << "服务器初始化成功，输出目录: ./dash" << std::endl;

    // 添加MP4文件
    std::cout << "\n[2] 添加MP4文件..." << std::endl;
    if (!server.addMP4File(mp4FilePath, streamName)) {
        std::cerr << "添加MP4文件失败: " << mp4FilePath << std::endl;
        return 1;
    }
    std::cout << "MP4文件添加成功: " << mp4FilePath << std::endl;

    // 启动服务器
    std::cout << "\n[3] 启动服务器..." << std::endl;
    if (!server.start()) {
        std::cerr << "启动DASH服务器失败" << std::endl;
        return 1;
    }

    // 显示访问信息
    std::cout << "\n=== 服务器已启动 ===" << std::endl;
    std::cout << "主页: http://localhost:" << port << "/" << std::endl;
    std::cout << "MPD文件: http://localhost:" << port << "/" << streamName << "/manifest.mpd" << std::endl;
    std::cout << "HTML播放器: ./dash/player.html" << std::endl;
    std::cout << "\n按Enter键停止服务器..." << std::endl;

    // 等待用户输入
    std::cin.get();

    // 停止服务器
    std::cout << "\n[4] 停止服务器..." << std::endl;
    server.stop();
    std::cout << "服务器已停止" << std::endl;

    return 0;
}