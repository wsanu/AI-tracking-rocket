/**
 * 慧眼串口通信协议控制程序
 * 支持协议 V3.1
 * 编译: gcc -o serial_control serial_control.c -lws2_32
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>
#include <time.h>

// 宏定义
#define MAX_BUFFER_SIZE 512
#define DEFAULT_BAUDRATE 115200
#define DEFAULT_PORT "COM9"

// 帧头帧尾定义
#define HEAD0_SEND 0x58
#define HEAD1_SEND 0x07
#define END_SEND 0x59
#define HEAD0_RESP 0x78
#define HEAD1_RESP 0x07
#define END_RESP 0x79

// 主命令字
#define CMD_PERIODIC     0x00
#define CMD_SYSTEM       0x01
#define CMD_SOFTWARE     0x02
#define CMD_ALGORITHM    0x03
#define CMD_OSD          0x04
#define CMD_TRANSPARENT  0x05

HANDLE hSerial = INVALID_HANDLE_VALUE;

// ============================================================
// 串口操作函数
// ============================================================

int serial_open(const char* port, int baudrate) {
    char portName[32];
    DCB dcb = {0};
    COMMTIMEOUTS timeouts = {0};
    
    sprintf(portName, "\\\\.\\%s", port);
    
    hSerial = CreateFileA(portName,
                          GENERIC_READ | GENERIC_WRITE,
                          0,
                          NULL,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL);
    
    if (hSerial == INVALID_HANDLE_VALUE) {
        printf("打开串口失败: %s\n", port);
        return -1;
    }
    
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(hSerial, &dcb)) {
        printf("获取串口状态失败\n");
        CloseHandle(hSerial);
        return -1;
    }
    
    dcb.BaudRate = baudrate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    
    if (!SetCommState(hSerial, &dcb)) {
        printf("设置串口状态失败\n");
        CloseHandle(hSerial);
        return -1;
    }
    
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    
    if (!SetCommTimeouts(hSerial, &timeouts)) {
        printf("设置超时失败\n");
        CloseHandle(hSerial);
        return -1;
    }
    
    printf("串口 %s 打开成功，波特率: %d\n", port, baudrate);
    return 0;
}

void serial_close(void) {
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
        printf("串口已关闭\n");
    }
}

int serial_send(const uint8_t* data, int len) {
    DWORD bytesWritten;
    
    if (hSerial == INVALID_HANDLE_VALUE) {
        printf("串口未打开\n");
        return -1;
    }
    
    printf("发送 %d 字节: ", len);
    for (int i = 0; i < len; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");
    
    if (!WriteFile(hSerial, data, len, &bytesWritten, NULL)) {
        printf("发送失败\n");
        return -1;
    }
    
    return (int)bytesWritten;
}

int serial_receive(uint8_t* buffer, int maxLen, int timeoutMs) {
    DWORD bytesRead;
    int totalRead = 0;
    DWORD startTime = GetTickCount();
    
    if (hSerial == INVALID_HANDLE_VALUE) {
        return -1;
    }
    
    while (totalRead < maxLen) {
        if (ReadFile(hSerial, buffer + totalRead, 1, &bytesRead, NULL)) {
            if (bytesRead > 0) {
                totalRead++;
                if (totalRead >= 6) {
                    DWORD t2 = GetTickCount();
                    while (GetTickCount() - t2 < 10) {
                        if (ReadFile(hSerial, buffer + totalRead, 1, &bytesRead, NULL) && bytesRead > 0) {
                            totalRead++;
                            if (totalRead >= maxLen) break;
                            t2 = GetTickCount();
                        }
                        Sleep(1);
                    }
                    break;
                }
            }
        }
        if (GetTickCount() - startTime > (DWORD)timeoutMs) {
            break;
        }
        Sleep(1);
    }
    return totalRead;
}

// ============================================================
// 协议封装函数
// ============================================================

uint8_t calc_check_sum(const uint8_t* data, int len) {
    uint32_t sum = 0;
    for (int i = 2; i < len - 2; i++) {
        sum += data[i];
    }
    return (uint8_t)(sum & 0xFF);
}

int build_frame(uint8_t cmd0, uint8_t cmd1, const uint8_t* data, int dataLen, uint8_t* frame) {
    int idx = 0;
    frame[idx++] = HEAD0_SEND;
    frame[idx++] = HEAD1_SEND;
    frame[idx++] = cmd0;
    frame[idx++] = cmd1;
    frame[idx++] = (uint8_t)dataLen;
    if (data && dataLen > 0) {
        memcpy(frame + idx, data, dataLen);
        idx += dataLen;
    }
    frame[idx] = calc_check_sum(frame, idx + 1);
    idx++;
    frame[idx++] = END_SEND;
    return idx;
}

int parse_response(const uint8_t* buffer, int len, uint8_t* respCmd0, uint8_t* respCmd1, 
                   uint8_t* respData, int* respDataLen) {
    if (len < 6) return -1;
    if (buffer[0] != HEAD0_RESP || buffer[1] != HEAD1_RESP) return -1;
    if (buffer[len - 1] != END_RESP) return -1;
    uint8_t calc = calc_check_sum(buffer, len);
    if (calc != buffer[len - 2]) {
        printf("校验和错误: 计算=0x%02X, 接收=0x%02X\n", calc, buffer[len - 2]);
        return -1;
    }
    *respCmd0 = buffer[2];
    *respCmd1 = buffer[3];
    *respDataLen = buffer[4];
    if (*respDataLen > 0 && respData) {
        memcpy(respData, buffer + 5, *respDataLen);
    }
    return 0;
}

// ============================================================
// 等待响应 - 改进版
// ============================================================

int wait_for_response(uint8_t* respData, int maxLen, int* respLen, int timeoutMs, uint8_t expectedCmd0, uint8_t expectedCmd1) {
    uint8_t buffer[MAX_BUFFER_SIZE];
    int totalLen = 0;
    DWORD startTime = GetTickCount();
    
    while (GetTickCount() - startTime < (DWORD)timeoutMs) {
        int len = serial_receive(buffer + totalLen, MAX_BUFFER_SIZE - totalLen - 1, 50);
        if (len > 0) {
            totalLen += len;
        }
        if (totalLen > 0) {
            int offset = 0;
            while (offset < totalLen - 6) {
                if (buffer[offset] == HEAD0_RESP && buffer[offset + 1] == HEAD1_RESP) {
                    int dataLen = buffer[offset + 4];
                    int frameLen = dataLen + 7;
                    
                    if (offset + frameLen <= totalLen && buffer[offset + frameLen - 1] == END_RESP) {
                        uint8_t calc = calc_check_sum(buffer + offset, frameLen);
                        if (calc == buffer[offset + frameLen - 2]) {
                            uint8_t cmd0 = buffer[offset + 2];
                            uint8_t cmd1 = buffer[offset + 3];
                            
                            if (expectedCmd0 != 0xFF || expectedCmd1 != 0xFF) {
                                if (cmd0 == expectedCmd0 && cmd1 == expectedCmd1) {
                                    *respLen = dataLen;
                                    if (dataLen > 0 && respData) {
                                        memcpy(respData, buffer + offset + 5, dataLen);
                                    }
                                    printf("响应: CMD0=0x%02X, CMD1=0x%02X, 数据长度=%d\n", cmd0, cmd1, dataLen);
                                    return 0;
                                }
                            } else {
                                *respLen = dataLen;
                                if (dataLen > 0 && respData) {
                                    memcpy(respData, buffer + offset + 5, dataLen);
                                }
                                printf("响应: CMD0=0x%02X, CMD1=0x%02X, 数据长度=%d\n", cmd0, cmd1, dataLen);
                                return 0;
                            }
                        }
                    }
                }
                offset++;
            }
        }
        Sleep(5);
    }
    
    *respLen = 0;
    return -1;
}

// ============================================================
// 协议命令封装
// ============================================================

// 4.1.2 自定义字符显示
int send_custom_text(int row, const char* text) {
    uint8_t data[130] = {0};
    uint8_t frame[140];
    int frameLen;
    int textLen = strlen(text);
    if (textLen > 128) textLen = 128;
    if (row > 3) row = 3;
    data[0] = (uint8_t)row;
    data[1] = (uint8_t)textLen;
    memcpy(data + 2, text, textLen);
    frameLen = build_frame(CMD_PERIODIC, 0x02, data, textLen + 2, frame);
    return serial_send(frame, frameLen);
}

// 4.2.1 系统时间设置
int send_system_time(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second, uint8_t ntpEnable) {
    uint8_t data[8];
    uint8_t frame[16];
    int frameLen;
    data[0] = year & 0xFF;
    data[1] = (year >> 8) & 0xFF;
    data[2] = month;
    data[3] = day;
    data[4] = hour;
    data[5] = minute;
    data[6] = second;
    data[7] = ntpEnable;
    frameLen = build_frame(CMD_SYSTEM, 0x01, data, 8, frame);
    return serial_send(frame, frameLen);
}

// 4.2.3 自检
int send_self_check(void) {
    uint8_t frame[8];
    int frameLen = build_frame(CMD_SYSTEM, 0x03, NULL, 0, frame);
    return serial_send(frame, frameLen);
}

// 4.2.4 设备信息查询
int send_device_info(void) {
    uint8_t frame[8];
    int frameLen = build_frame(CMD_SYSTEM, 0x04, NULL, 0, frame);
    return serial_send(frame, frameLen);
}

// 4.2.5 设备重启
int send_device_reboot(void) {
    uint8_t frame[8];
    int frameLen = build_frame(CMD_SYSTEM, 0x05, NULL, 0, frame);
    return serial_send(frame, frameLen);
}

// 4.3.1 主通道切换
int send_channel_switch(uint8_t channel) {
    uint8_t data[2] = {channel, 0};
    uint8_t frame[12];
    int frameLen = build_frame(CMD_SOFTWARE, 0x01, data, 2, frame);
    return serial_send(frame, frameLen);
}

// 4.3.2 画中画控制
int send_pip_control(uint8_t enable) {
    uint8_t data[2] = {enable, 0};
    uint8_t frame[12];
    int frameLen = build_frame(CMD_SOFTWARE, 0x02, data, 2, frame);
    return serial_send(frame, frameLen);
}

// 4.3.3 拍照
int send_capture(void) {
    uint8_t data[2] = {0, 0};
    uint8_t frame[12];
    int frameLen = build_frame(CMD_SOFTWARE, 0x11, data, 2, frame);
    return serial_send(frame, frameLen);
}

// 4.3.4 录像控制
int send_record_control(uint8_t enable) {
    uint8_t data[2] = {enable, 0};
    uint8_t frame[12];
    int frameLen = build_frame(CMD_SOFTWARE, 0x12, data, 2, frame);
    return serial_send(frame, frameLen);
}

// 4.3.5 拍照录像文件控制
int send_file_control(uint8_t operation) {
    uint8_t data[2] = {operation, 0};
    uint8_t frame[12];
    int frameLen = build_frame(CMD_SOFTWARE, 0x13, data, 2, frame);
    return serial_send(frame, frameLen);
}

// 4.3.6 电子变倍
int send_zoom_control(uint8_t enable, uint8_t zoom) {
    uint8_t data[2] = {enable, zoom};
    uint8_t frame[12];
    int frameLen = build_frame(CMD_SOFTWARE, 0x21, data, 2, frame);
    return serial_send(frame, frameLen);
}

// 4.4.1 目标检测控制
int send_detection_control(uint8_t mode) {
    uint8_t data[2] = {mode, 0};
    uint8_t frame[12];
    int frameLen = build_frame(CMD_ALGORITHM, 0x01, data, 2, frame);
    return serial_send(frame, frameLen);
}

// 4.4.5 目标检测后自动锁定
int send_auto_lock(uint8_t mode, uint8_t strategy) {
    uint8_t data[4] = {mode, strategy, 0, 0};
    uint8_t frame[14];
    int frameLen = build_frame(CMD_ALGORITHM, 0x05, data, 4, frame);
    return serial_send(frame, frameLen);
}

// 4.4.6 跟踪使能控制
int send_track_control(uint8_t mode, uint8_t targetId, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    uint8_t data[10];
    uint8_t frame[20];
    int frameLen;
    data[0] = mode;
    data[1] = targetId;
    data[2] = x & 0xFF;
    data[3] = (x >> 8) & 0xFF;
    data[4] = y & 0xFF;
    data[5] = (y >> 8) & 0xFF;
    data[6] = w & 0xFF;
    data[7] = (w >> 8) & 0xFF;
    data[8] = h & 0xFF;
    data[9] = (h >> 8) & 0xFF;
    frameLen = build_frame(CMD_ALGORITHM, 0x11, data, 10, frame);
    return serial_send(frame, frameLen);
}

// 4.4.11 十字位置控制
int send_cross_position(uint16_t x, uint16_t y) {
    uint8_t data[5];
    uint8_t frame[14];
    int frameLen;
    data[0] = 0x03;
    data[1] = x & 0xFF;
    data[2] = (x >> 8) & 0xFF;
    data[3] = y & 0xFF;
    data[4] = (y >> 8) & 0xFF;
    frameLen = build_frame(CMD_ALGORITHM, 0x1A, data, 5, frame);
    return serial_send(frame, frameLen);
}

// 4.5.4 OSD颜色控制
int send_osd_color(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t data[4] = {1, r, g, b};
    uint8_t frame[14];
    int frameLen = build_frame(CMD_OSD, 0x04, data, 4, frame);
    return serial_send(frame, frameLen);
}

// ============================================================
// 帮助
// ============================================================

void print_usage(const char* progName) {
    printf("\n慧眼串口通信协议控制程序 V3.1\n");
    printf("用法: %s [COM口] [命令] [参数...]\n", progName);
    printf("\n=== 系统设置 ===\n");
    printf("  info                    - 查询设备信息\n");
    printf("  check                   - 自检\n");
    printf("  reboot                  - 设备重启\n");
    printf("  time <年> <月> <日> <时> <分> <秒> [NTP] - 设置时间\n");
    printf("\n=== 软件功能 ===\n");
    printf("  switch <0/1>            - 切换主通道 (0-可见光, 1-红外)\n");
    printf("  pip <0/1>               - 画中画控制\n");
    printf("  capture                 - 拍照\n");
    printf("  record <0/1>            - 录像控制 (0-停止, 1-开始)\n");
    printf("  file <操作>             - 文件控制 (1-删照片, 2-删录像, 3-查数量, 4-查空间)\n");
    printf("  zoom <0/1> <倍率>       - 电子变倍 (倍率: 10-100, 10=1.0倍)\n");
    printf("\n=== 算法功能 ===\n");
    printf("  detect <0/1/2>          - 目标检测 (0-关闭, 1-开启, 2-开启+多目标跟踪)\n");
    printf("  autolock <mode> <strategy> - 自动锁定\n");
    printf("    mode: 0-关闭, 1-单次, 2-循环\n");
    printf("    strategy: 0-按置信度, 1-按位置\n");
    printf("  track <mode> <x> <y> <w> <h> - 跟踪控制\n");
    printf("    mode: 0-停止, 1-框选, 2-ID跟踪, 3-点选\n");
    printf("  cross <x> <y>           - 设置十字位置\n");
    printf("\n=== OSD设置 ===\n");
    printf("  color <R> <G> <B>       - OSD颜色 (RGB 0-255)\n");
    printf("\n=== 其他 ===\n");
    printf("  text <行号> <字符串>    - 自定义字符显示 (行号1-3)\n");
    printf("  help                    - 显示帮助\n");
    printf("\n示例:\n");
    printf("  %s COM9 info\n", progName);
    printf("  %s COM9 detect 2\n", progName);
    printf("  %s COM9 autolock 2 0\n", progName);
    printf("  %s COM9 track 1 960 540 100 100\n", progName);
    printf("  %s COM9 text 1 \"Hello World\"\n", progName);
}

// ============================================================
// 主程序
// ============================================================

int main(int argc, char* argv[]) {
    char portName[16] = DEFAULT_PORT;
    uint8_t respData[256];
    int respLen;
    
    printf("========================================\n");
    printf("  慧眼串口通信协议控制程序 V3.1\n");
    printf("========================================\n");
    
    if (argc < 2) {
        print_usage(argv[0]);
        return 0;
    }
    
    // 检查是否指定了COM口
    if (strncmp(argv[1], "COM", 3) == 0 || strncmp(argv[1], "com", 3) == 0) {
        strcpy(portName, argv[1]);
        if (argc < 3) {
            print_usage(argv[0]);
            return 0;
        }
        argv++;
        argc--;
    }
    
    if (serial_open(portName, DEFAULT_BAUDRATE) != 0) {
        printf("打开串口失败\n");
        return -1;
    }
    
    const char* cmd = argv[1];
    int result = 0;
    
    // ========== 系统设置 ==========
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0 || strcmp(cmd, "?") == 0) {
        print_usage(argv[0]);
    }
    else if (strcmp(cmd, "info") == 0) {
        printf("发送: 设备信息查询\n");
        send_device_info();
        if (wait_for_response(respData, 256, &respLen, 2000, 0x01, 0x84) == 0 && respLen >= 16) {
            printf("  项目名: %c%c\n", respData[0], respData[1]);
            printf("  项目编号: %d\n", respData[2]);
            printf("  生产年份: %d\n", respData[3] + 2000);
            printf("  生产月份: %d\n", respData[4]);
            printf("  生产批次: %d\n", respData[5]);
            printf("  设备编号: %d\n", respData[6] | (respData[7] << 8));
            printf("  平台: %c%c%c%c\n", respData[8], respData[9], respData[10], respData[11]);
            printf("  软件版本: %d.%d.%d.%d\n", respData[12], respData[13], respData[14], respData[15]);
        }
    }
    else if (strcmp(cmd, "check") == 0) {
        printf("发送: 自检\n");
        send_self_check();
        wait_for_response(respData, 256, &respLen, 2000, 0x01, 0x83);
    }
    else if (strcmp(cmd, "reboot") == 0) {
        printf("发送: 设备重启\n");
        send_device_reboot();
        wait_for_response(respData, 256, &respLen, 2000, 0x01, 0x85);
    }
    else if (strcmp(cmd, "time") == 0 && argc >= 8) {
        uint16_t year = (uint16_t)atoi(argv[2]);
        uint8_t month = (uint8_t)atoi(argv[3]);
        uint8_t day = (uint8_t)atoi(argv[4]);
        uint8_t hour = (uint8_t)atoi(argv[5]);
        uint8_t minute = (uint8_t)atoi(argv[6]);
        uint8_t second = (uint8_t)atoi(argv[7]);
        uint8_t ntp = (argc >= 9) ? (uint8_t)atoi(argv[8]) : 0;
        printf("发送: 设置时间 %04d-%02d-%02d %02d:%02d:%02d NTP=%d\n", year, month, day, hour, minute, second, ntp);
        send_system_time(year, month, day, hour, minute, second, ntp);
        wait_for_response(respData, 256, &respLen, 1000, 0x01, 0x81);
    }
    
    // ========== 软件功能 ==========
    else if (strcmp(cmd, "switch") == 0 && argc >= 3) {
        uint8_t channel = (uint8_t)atoi(argv[2]);
        printf("发送: 切换通道 %d (%s)\n", channel, channel == 0 ? "可见光" : "红外");
        send_channel_switch(channel);
        wait_for_response(respData, 256, &respLen, 1000, 0x02, 0x81);
    }
    else if (strcmp(cmd, "pip") == 0 && argc >= 3) {
        uint8_t enable = (uint8_t)atoi(argv[2]);
        printf("发送: 画中画 %s\n", enable ? "开启" : "关闭");
        send_pip_control(enable);
        wait_for_response(respData, 256, &respLen, 1000, 0x02, 0x82);
    }
    else if (strcmp(cmd, "capture") == 0) {
        printf("发送: 拍照\n");
        send_capture();
        wait_for_response(respData, 256, &respLen, 1000, 0x02, 0x91);
    }
    else if (strcmp(cmd, "record") == 0 && argc >= 3) {
        uint8_t enable = (uint8_t)atoi(argv[2]);
        printf("发送: %s录像\n", enable ? "开始" : "停止");
        send_record_control(enable);
        wait_for_response(respData, 256, &respLen, 1000, 0x02, 0x92);
    }
    else if (strcmp(cmd, "file") == 0 && argc >= 3) {
        uint8_t op = (uint8_t)atoi(argv[2]);
        const char* opStr[] = {"", "删除拍照文件", "删除录像文件", "查询文件数量", "查询磁盘空间"};
        printf("发送: %s\n", op >= 1 && op <= 4 ? opStr[op] : "未知操作");
        send_file_control(op);
        if (wait_for_response(respData, 256, &respLen, 1000, 0x02, 0x93) == 0) {
            if (op == 3 && respLen >= 6) {
                uint16_t photoCnt = respData[2] | (respData[3] << 8);
                uint16_t videoCnt = respData[4] | (respData[5] << 8);
                printf("照片数量: %d, 录像数量: %d\n", photoCnt, videoCnt);
            }
            if (op == 4 && respLen >= 6) {
                uint16_t space = respData[2] | (respData[3] << 8);
                printf("剩余磁盘空间: %.1f GB\n", space / 10.0f);
            }
        }
    }
    else if (strcmp(cmd, "zoom") == 0 && argc >= 4) {
        uint8_t enable = (uint8_t)atoi(argv[2]);
        uint8_t zoom = (uint8_t)atoi(argv[3]);
        printf("发送: 电子变倍 %s, 倍率 %d.%d倍\n", enable ? "开启" : "关闭", zoom/10, zoom%10);
        send_zoom_control(enable, zoom);
        wait_for_response(respData, 256, &respLen, 1000, 0x02, 0xA1);
    }
    
    // ========== 算法功能 ==========
    else if (strcmp(cmd, "detect") == 0 && argc >= 3) {
        uint8_t mode = (uint8_t)atoi(argv[2]);
        const char* modeStr[] = {"关闭", "开启目标检测", "开启+多目标跟踪"};
        printf("发送: 目标检测 %s\n", mode <= 2 ? modeStr[mode] : "未知");
        send_detection_control(mode);
        wait_for_response(respData, 256, &respLen, 1000, 0x03, 0x81);
    }
    else if (strcmp(cmd, "autolock") == 0 && argc >= 4) {
        uint8_t mode = (uint8_t)atoi(argv[2]);
        uint8_t strategy = (uint8_t)atoi(argv[3]);
        const char* modeStr[] = {"关闭", "单次", "循环"};
        const char* strategyStr[] = {"按置信度", "按位置"};
        printf("发送: 自动锁定 %s, 策略 %s\n", 
               mode <= 2 ? modeStr[mode] : "未知",
               strategy <= 1 ? strategyStr[strategy] : "未知");
        send_auto_lock(mode, strategy);
        wait_for_response(respData, 256, &respLen, 1000, 0x03, 0x85);
    }
    else if (strcmp(cmd, "track") == 0 && argc >= 7) {
        uint8_t mode = (uint8_t)atoi(argv[2]);
        uint8_t targetId = (uint8_t)atoi(argv[3]);
        uint16_t x = (uint16_t)atoi(argv[4]);
        uint16_t y = (uint16_t)atoi(argv[5]);
        uint16_t w = (uint16_t)atoi(argv[6]);
        uint16_t h = (uint16_t)atoi(argv[7]);
        const char* modeStr[] = {"停止", "框选跟踪", "ID跟踪", "点选跟踪"};
        printf("发送: 跟踪控制 %s", mode <= 3 ? modeStr[mode] : "未知");
        if (mode == 2) printf(" (目标ID=%d)", targetId);
        if (mode == 1 || mode == 3) printf(" pos=(%d,%d) size=(%d,%d)", x, y, w, h);
        printf("\n");
        send_track_control(mode, targetId, x, y, w, h);
        wait_for_response(respData, 256, &respLen, 1000, 0x03, 0x91);
    }
    else if (strcmp(cmd, "cross") == 0 && argc >= 4) {
        uint16_t x = (uint16_t)atoi(argv[2]);
        uint16_t y = (uint16_t)atoi(argv[3]);
        printf("发送: 设置十字位置 (%d, %d)\n", x, y);
        send_cross_position(x, y);
        wait_for_response(respData, 256, &respLen, 1000, 0x03, 0x9A);
    }
    
    // ========== OSD设置 ==========
    else if (strcmp(cmd, "color") == 0 && argc >= 5) {
        uint8_t r = (uint8_t)atoi(argv[2]);
        uint8_t g = (uint8_t)atoi(argv[3]);
        uint8_t b = (uint8_t)atoi(argv[4]);
        printf("发送: 设置OSD颜色 RGB(%d,%d,%d)\n", r, g, b);
        send_osd_color(r, g, b);
        wait_for_response(respData, 256, &respLen, 1000, 0x04, 0x84);
    }
    
    // ========== 其他 ==========
    else if (strcmp(cmd, "text") == 0 && argc >= 4) {
        int row = atoi(argv[2]);
        const char* text = argv[3];
        printf("发送: 自定义字符显示 行%d: %s\n", row, text);
        send_custom_text(row, text);
        wait_for_response(respData, 256, &respLen, 1000, 0x00, 0x82);
    }
    else {
        printf("未知命令: %s\n", cmd);
        print_usage(argv[0]);
    }
    
    serial_close();
    return 0;
}