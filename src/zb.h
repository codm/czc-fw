bool zbFwCheck();
void zbHwCheck();
bool zbLedToggle();
bool zigbeeErase();
void nvPrgs(const String &inputMsg);
void zbEraseNV(void *pvParameters);

bool flashZigbeefromURL(const char *url, const char *filePath, CCTools &CCTool);
const char* downloadFirmwareFromGithub(const char *url);
bool eraseWriteZbFile(const char *filePath, CCTools &CCTool);
float sendPercentageToFrontend(float percent, float previousPercent, const char* eventType);
bool hasEnoughLittleFsSpaceLeft(size_t firmwareSize);
bool removeFileFromFS(const char *filePath);