#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <ETH.h>
#include <HTTPClient.h>

#include <CCTools.h>

#include "config.h"
#include "web.h"
#include "log.h"
#include "etc.h"
#include "zb.h"
#include "mqtt.h"
#include "const/keys.h"

extern struct SysVarsStruct vars;
extern struct HwConfigStruct hwConfig;
extern HwBrdConfigStruct brdConfigs[BOARD_CFG_CNT];

extern struct SystemConfigStruct systemCfg;
extern struct NetworkConfigStruct networkCfg;
extern struct VpnConfigStruct vpnCfg;
extern struct MqttConfigStruct mqttCfg;

extern LEDControl ledControl;

extern const char *tempFile;

size_t lastSize = 0;

String tag_ZB = "[ZB]";

extern CCTools CCTool;

bool zbFwCheck()
{
    const int maxAttempts = 3;
    for (int attempt = 0; attempt < maxAttempts; attempt++)
    {
        if (CCTool.checkFirmwareVersion())
        {
            printLogMsg(tag_ZB + " FW: " + String(CCTool.chip.fwRev));
            return true;
        }
        else
        {
            CCTool.restart();
            int val = attempt + 1;
            LOGD("Try: %d", val);
            delay(500 * (val * val));
        }
    }
    printLogMsg(tag_ZB + " FW: Unknown! Check serial speed!");
    return false;
}

void zbHwCheck()
{
    ledControl.modeLED.mode = LED_BLINK_1Hz;

    if (CCTool.detectChipInfo())
    {
        printLogMsg(tag_ZB + " Chip: " + CCTool.chip.hwRev);
        printLogMsg(tag_ZB + " IEEE: " + CCTool.chip.ieee);
        LOGI("modeCfg %s", String((CCTool.chip.modeCfg), HEX));
        LOGI("bslCfg %s", String((CCTool.chip.bslCfg), HEX));
        printLogMsg(tag_ZB + " Flash size: " + String(CCTool.chip.flashSize / 1024) + " KB");

        vars.hwZigbeeIs = true;
        ledControl.modeLED.mode = LED_OFF;
    }
    else
    {
        printLogMsg(tag_ZB + " No Zigbee chip!");
        vars.hwZigbeeIs = false;
        ledControl.modeLED.mode = LED_BLINK_3Hz;
    }
    CCTool.restart();
}

bool zbLedToggle()
{
    if (CCTool.ledToggle())
    {
        if (CCTool.ledState == 1)
        {
            printLogMsg("[ZB] LED toggle ON");
            // vars.zbLedState = 1;
        }
        else
        {
            printLogMsg("[ZB] LED toggle OFF");
            // vars.zbLedState = 0;
        }
        return true;
    }
    else
    {
        printLogMsg("[ZB] LED toggle ERROR");
        return false;
    }
}

bool zigbeeErase()
{
    if (CCTool.eraseFlash())
    {
        LOGD("magic");
        return true;
    }
    return false;
}
void nvPrgs(const String &inputMsg)
{

    const uint8_t eventLen = 30;
    String msg = inputMsg;
    if (msg.length() > 25)
    {
        msg = msg.substring(0, 25);
    }
    sendEvent(tagZB_NV_prgs, eventLen, msg);
    LOGD("%s", msg.c_str());
}

void zbEraseNV(void *pvParameters)
{
    vars.zbFlashing = true;
    CCTool.nvram_reset(nvPrgs);
    logClear();
    printLogMsg("NVRAM erase finish! Restart CC2652!");
    vars.zbFlashing = false;
    vTaskDelete(NULL);
}

bool flashZigbeefromURL(const char *url, const char *zigbee_firmware_path, CCTools &CCTool) {
    const uint8_t eventLen = 11;
    bool update_successful = false;

    ledControl.modeLED.mode = LED_BLINK_3Hz;
    vars.zbFlashing = true;

    // Remove first if the previous firmware download had an error and the file was not deleted
    removeFileFromFS(zigbee_firmware_path);
    zigbee_firmware_path = downloadFirmwareFromGithub(url);
    if(zigbee_firmware_path != nullptr) {
        update_successful = eraseWriteZbFile(zigbee_firmware_path,CCTool) && removeFileFromFS(zigbee_firmware_path);  
        if(!update_successful) {
            DEBUG_PRINTLN("[FLASH] Error while flashing or erasing file -> function returned false");
        }
    }
    else {
        sendEvent(tagZB_FW_err, eventLen, String("Failed!"));
        DEBUG_PRINTLN("[HTTP] download returned file nullptr");
    }

    ledControl.modeLED.mode = LED_OFF;
    vars.zbFlashing = false;
    return update_successful;
}

const char* downloadFirmwareFromGithub(const char *url) {
    const uint8_t eventLen = 11;
    sendEvent(tagZB_FW_info, eventLen, String("startDownload"));

    HTTPClient http;
    WiFiClientSecure secure_client;
    secure_client.setInsecure();
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    const char* zigbee_firmware_path = "/zigbee/firmware.bin";
    DEBUG_PRINTLN("[HTTP] begin...");
    http.begin(secure_client, url);

    http.addHeader("Content-Type", "application/octet-stream");
    http.addHeader("Connection", "close");    

    int http_responce_code = http.GET();
    DEBUG_PRINT("[HTTP] GET code: ");
    DEBUG_PRINTLN(http_responce_code);
    if(http_responce_code == HTTP_CODE_OK) {
        int http_remaining_file_length = http.getSize();
        int http_total_file_length = http_remaining_file_length;
        DEBUG_PRINT("[HTTP] GET file length: ");
        DEBUG_PRINTLN(http_remaining_file_length);

        uint8_t http_read_buffer[128] = {0};

        WiFiClient* http_read_stream = http.getStreamPtr();

        DEBUG_PRINTLN("[LITTLEFS] check filesystem, create and open Firmware file");
        if(!hasEnoughLittleFsSpaceLeft(725000)) {
            DEBUG_PRINTLN("[LITTLEFS] ERROR: not enough space in FS, returning nullptr");
            return nullptr;
        }

        LittleFS.mkdir("/zigbee");
        LittleFS.open(zigbee_firmware_path, "w");
        DEBUG_PRINTLN("[LITTLEFS] file creation and checkout completed successfully");

        File zigbee_firmware_file = LittleFS.open(zigbee_firmware_path, FILE_APPEND);
        if(!zigbee_firmware_file) {
            DEBUG_PRINTLN("[LITTLEFS] ERROR opening firmware file");
            return nullptr;
        }

        DEBUG_PRINTLN("[HTTP] Start download...");
        float previousPercent = 0;
        // download remaining file length OR continue chunked download (len = -1)
        while(http.connected() && (http_remaining_file_length > 0 || http_remaining_file_length == -1)) {
            size_t download_available_size = http_read_stream->available();

            if(download_available_size > 0) {
                int http_payload_size = http_read_stream->readBytes(
                    http_read_buffer, 
                    ((download_available_size > sizeof(http_read_buffer)) ? sizeof(http_read_buffer) : download_available_size)
                );

                //write payload to littleFS
                if(!zigbee_firmware_file.write(http_read_buffer, http_payload_size)){
                    DEBUG_PRINTLN("[LITTLEFS] ERROR appending data to firmware file");
                    return nullptr;
                }

                if(http_remaining_file_length > 0)
                    http_remaining_file_length -= http_payload_size;
            }

            float percent = ((float)http_total_file_length - http_remaining_file_length) / http_total_file_length * 100.0f;  
            previousPercent = sendPercentageToFrontend(percent, previousPercent, tagZB_FW_prgs);

            delay(1); // yield to other applications
        }
        zigbee_firmware_file.close();
        DEBUG_PRINTLN("[HTTP] Finished download");
    }
    else {
        DEBUG_PRINT("[HTTP] GET failed, error: ");
        DEBUG_PRINTLN(http.errorToString(http_responce_code).c_str());
    }
    http.end();
    return zigbee_firmware_path;
}

bool eraseWriteZbFile(const char *filePath, CCTools &CCTool)
{
    const uint8_t eventLen = 11;
    sendEvent(tagZB_FW_info, eventLen, String("startFlash"));
    
    File file = LittleFS.open(filePath, "r");
    if (!file)
    {
        char buffer[100];
        snprintf(buffer, sizeof(buffer), "Failed to open file: %s\n", filePath);
        printLogMsg(buffer);
        return false;
    }

    CCTool.eraseFlash();
    printLogMsg("Erase completed!");
    DEBUG_PRINTLN("[FLASH] Erase completed!");

    int totalSize = file.size();

    if (!CCTool.beginFlash(BEGIN_ZB_ADDR, totalSize))
    {
        file.close();
        return false;
    }

    byte buffer[CCTool.TRANSFER_SIZE];
    int loadedSize = 0;

    DEBUG_PRINTLN("[FLASH] Begin flash process...");
    float previousPercent = 0;
    while (file.available() && loadedSize < totalSize)
    {
        size_t size = file.available();
        int c = file.readBytes(reinterpret_cast<char *>(buffer), std::min(size, sizeof(buffer)));
        CCTool.processFlash(buffer, c);
        loadedSize += c;
        float percent = static_cast<float>(loadedSize) / totalSize * 100.0f;
        previousPercent = sendPercentageToFrontend(percent, previousPercent, tagZB_FW_prgs);
        delay(1); // Yield to allow other processes
    }
    
    DEBUG_PRINTLN("[FLASH] Completed!");
    file.close();

    sendEvent(tagZB_FW_info, eventLen, String("finishFlash"));

    CCTool.restart();
    return true;
}

float sendPercentageToFrontend(float percent, float previousPercent, const char* eventType) {
    const uint8_t eventLen = 11;

    if ((percent - previousPercent) > 1 || percent < 0.1 || percent == 100) {
        sendEvent(eventType, eventLen, String(percent));  
        DEBUG_PRINT("[DOWNLOAD/FLASH] in progress: ");
        DEBUG_PRINTLN(percent);  
        return percent;
    }
    return previousPercent;
}

bool hasEnoughLittleFsSpaceLeft(size_t firmwareSize) {
    size_t remainingBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
    return remainingBytes > firmwareSize;
}

bool removeFileFromFS(const char *filePath) {
    DEBUG_PRINTLN("[LITTLEFS] removing file");
    return LittleFS.remove(filePath);
}