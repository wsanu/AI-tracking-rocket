/**
 * 慧眼串口监听程序 V2.0 - 状态机版
 * 持续流式读取，实时解析
 * 编译: gcc -o serial_monitor serial_monitor.c -lws2_32
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>
#include <time.h>

#define MAX_BUFFER_SIZE 4096
#define DEFAULT_BAUDRATE 115200
#define DEFAULT_PORT "COM9"

#define HEAD0_RESP 0x78
#define HEAD1_RESP 0x07
#define END_RESP 0x79

HANDLE hSerial = INVALID_HANDLE_VALUE;

// ============================================================
// 串口操作
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
    
    // 超时设置：读间隔1ms，总超时10ms（快速响应）
    timeouts.ReadIntervalTimeout = 1;
    timeouts.ReadTotalTimeoutConstant = 10;
    timeouts.ReadTotalTimeoutMultiplier = 1;
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
    }
}

// ============================================================
// 校验和计算
// ============================================================

uint8_t calc_check_sum(const uint8_t* data, int len) {
    uint32_t sum = 0;
    for (int i = 2; i < len - 2; i++) {
        sum += data[i];
    }
    return (uint8_t)(sum & 0xFF);
}

// ============================================================
// 报文解析函数
// ============================================================

void parse_offset_data(const uint8_t* data, int len) {
    if (len < 14) return;
    
    uint8_t status = data[0];
    uint8_t channel = data[1];
    int32_t offsetX = *(int32_t*)(data + 2);
    int32_t offsetY = *(int32_t*)(data + 6);
    uint16_t width = *(uint16_t*)(data + 10);
    uint16_t height = *(uint16_t*)(data + 12);
    
    printf("[测偏] 状态=0x%02X 通道=%d 偏移=(%d,%d) 大小=%dx%d ",
           status, channel, offsetX, offsetY, width, height);
    printf("bit2=%d bit1=%d(%s) bit0=%d(%s)\n",
           (status >> 2) & 1,
           (status >> 1) & 1, ((status >> 1) & 1) ? "停止" : "运行",
           status & 1, (status & 1) ? "有效" : "无效");
}

void parse_ai_detection(const uint8_t* data, int len) {
    if (len < 3) return;
    
    uint8_t frameId = data[0];
    uint8_t totalTargets = data[1];
    uint8_t currentTargets = data[2];
    
    printf("[AI检测] 帧ID=%d 总目标=%d 当前=%d", frameId, totalTargets, currentTargets);
    
    int offset = 3;
    for (int i = 0; i < currentTargets && offset + 11 <= len; i++) {
        uint8_t id = data[offset];
        uint8_t type = data[offset + 1];
        uint8_t confidence = data[offset + 2];
        uint16_t x = *(uint16_t*)(data + offset + 3);
        uint16_t y = *(uint16_t*)(data + offset + 5);
        uint16_t w = *(uint16_t*)(data + offset + 7);
        uint16_t h = *(uint16_t*)(data + offset + 9);
        printf(" [ID=%d 类型=%d 置信=%d%% (%d,%d)%dx%d]", id, type, confidence, x, y, w, h);
        offset += 11;
    }
    printf("\n");
}

void parse_heartbeat(const uint8_t* data, int len) {
    if (len < 6) return;
    
    uint16_t count = *(uint16_t*)(data + 0);
    uint32_t errorCode = *(uint32_t*)(data + 2);
    
    printf("[心跳] 计数=%d 自检码=0x%08X %s\n", count, errorCode, errorCode == 0 ? "(正常)" : "(故障)");
}

void parse_general_response(const uint8_t* data, int len, uint8_t cmd0, uint8_t cmd1) {
    printf("[响应] CMD=0x%02X%02X 长度=%d ", cmd0, cmd1, len);
    if (len > 0) {
        printf("数据:");
        for (int i = 0; i < len && i < 8; i++) {
            printf(" %02X", data[i]);
        }
        if (len > 8) printf(" ...");
    }
    printf("\n");
}

// ============================================================
// 状态机解析器
// ============================================================

typedef struct {
    uint8_t buffer[MAX_BUFFER_SIZE];
    int bufLen;
    int packetCount;
} ParserState;

void parser_init(ParserState* state) {
    state->bufLen = 0;
    state->packetCount = 0;
    memset(state->buffer, 0, MAX_BUFFER_SIZE);
}

void parser_feed(ParserState* state, const uint8_t* data, int len) {
    // 追加到缓冲区
    if (state->bufLen + len > MAX_BUFFER_SIZE) {
        // 缓冲区满了，丢掉最旧的一半数据
        int keep = MAX_BUFFER_SIZE / 2;
        memmove(state->buffer, state->buffer + state->bufLen - keep, keep);
        state->bufLen = keep;
    }
    memcpy(state->buffer + state->bufLen, data, len);
    state->bufLen += len;
    
    // 解析缓冲区中的所有完整报文
    int offset = 0;
    while (offset < state->bufLen - 6) {
        // 查找帧头 0x78 0x07
        if (state->buffer[offset] == HEAD0_RESP && state->buffer[offset + 1] == HEAD1_RESP) {
            int dataLen = state->buffer[offset + 4];
            int frameLen = dataLen + 7;
            
            if (offset + frameLen <= state->bufLen && state->buffer[offset + frameLen - 1] == END_RESP) {
                uint8_t calc = calc_check_sum(state->buffer + offset, frameLen);
                uint8_t recv = state->buffer[offset + frameLen - 2];
                
                if (calc == recv) {
                    state->packetCount++;
                    uint8_t cmd0 = state->buffer[offset + 2];
                    uint8_t cmd1 = state->buffer[offset + 3];
                    
                    // 打印（简洁模式）
                    if (cmd0 == 0x00 && cmd1 == 0x81) {
                        parse_offset_data(state->buffer + offset + 5, dataLen);
                    } else if (cmd0 == 0x00 && cmd1 == 0x82) {
                        parse_ai_detection(state->buffer + offset + 5, dataLen);
                    } else if (cmd0 == 0x00 && cmd1 == 0x83) {
                        parse_heartbeat(state->buffer + offset + 5, dataLen);
                    } else {
                        parse_general_response(state->buffer + offset + 5, dataLen, cmd0, cmd1);
                    }
                    
                    offset += frameLen;
                    continue;
                }
            }
        }
        offset++;
    }
    
    // 移除已处理的数据（保留未完成的尾部）
    if (offset > 0) {
        memmove(state->buffer, state->buffer + offset, state->bufLen - offset);
        state->bufLen -= offset;
    }
}

// ============================================================
// 主程序
// ============================================================

int main(int argc, char* argv[]) {
    char portName[16] = DEFAULT_PORT;
    uint8_t readBuffer[1024];
    ParserState parser;
    DWORD bytesRead;
    int totalPackets = 0;
    DWORD lastPrintTime = GetTickCount();
    
    printf("========================================\n");
    printf("  慧眼串口监听程序 V2.0 (状态机版)\n");
    printf("  持续流式读取，实时解析\n");
    printf("========================================\n");
    
    if (argc >= 2) {
        if (strncmp(argv[1], "COM", 3) == 0 || strncmp(argv[1], "com", 3) == 0) {
            strcpy(portName, argv[1]);
        }
    }
    
    printf("监听端口: %s\n", portName);
    printf("按 Ctrl+C 退出\n");
    printf("========================================\n\n");
    
    if (serial_open(portName, DEFAULT_BAUDRATE) != 0) {
        printf("打开串口失败\n");
        return -1;
    }
    
    parser_init(&parser);
    
    // 清空串口缓冲区
    PurgeComm(hSerial, PURGE_RXCLEAR);
    
    printf("开始监听...\n\n");
    
    while (1) {
        // 持续读取（非阻塞）
        if (ReadFile(hSerial, readBuffer, sizeof(readBuffer), &bytesRead, NULL)) {
            if (bytesRead > 0) {
                parser_feed(&parser, readBuffer, bytesRead);
                totalPackets += parser.packetCount;
                
                // 每秒打印一次统计
                if (GetTickCount() - lastPrintTime > 1000) {
                    // 不额外打印，保持输出干净
                    lastPrintTime = GetTickCount();
                }
            }
        }
        Sleep(1);  // 让出CPU
    }
    
    serial_close();
    return 0;
}