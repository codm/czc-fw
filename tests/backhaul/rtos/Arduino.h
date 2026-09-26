#pragma once
#include "../shims/Arduino.h"
#include <mutex>
#include <deque>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
enum hardwareSerial_error_t {
    UART_NO_ERROR,UART_BREAK_ERROR,UART_BUFFER_FULL_ERROR,UART_FIFO_OVF_ERROR,UART_FRAME_ERROR,UART_PARITY_ERROR
};
class SerialFixture {
    std::mutex mutex;
    std::deque<uint8_t> input;
public:
    std::function<void(const uint8_t *,size_t)> onWrite;
    std::function<void(hardwareSerial_error_t)> onError;
    std::atomic<bool> pauseReads{false},readPaused{false};
    unsigned writes=0,reads=0;
    int available() {
        if(pauseReads) {
            readPaused=true;
            while(pauseReads) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            readPaused=false;
        }
        std::lock_guard<std::mutex> lock(mutex); return input.size();
    }
    int read() { std::lock_guard<std::mutex> lock(mutex); if(input.empty()) return -1; int b=input.front(); input.pop_front(); ++reads; return b; }
    size_t write(const uint8_t *p,size_t n) { ++writes; if(onWrite) onWrite(p,n); return n; }
    bool setRxFIFOFull(uint8_t) { return true; }
    void eventQueueReset() {}
    void onReceiveError(std::function<void(hardwareSerial_error_t)> cb) { onError=cb; }
    void error(hardwareSerial_error_t e) { if(onError) onError(e); }
    void feed(const uint8_t *p,size_t n) { std::lock_guard<std::mutex> lock(mutex); input.insert(input.end(),p,p+n); }
};
extern SerialFixture Serial2;
