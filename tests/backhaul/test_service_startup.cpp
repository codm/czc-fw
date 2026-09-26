#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using String=std::string;
#define LOGD(...) do {} while(0)
#define LOGI(...) do {} while(0)
#define LOGW(...) do {} while(0)
static const int WORK_MODE_NETWORK=0,WORK_MODE_USB=1,STOPPED=0;
static const int WIFI_STA=1,WIFI_AP_STA=2,LED_ON=1,LED_BLINK_1Hz=2,ZIGBEE=1,XZG=2;
struct IPAddress { IPAddress(int,int,int,int) {} };
static IPAddress apIP(192,168,1,1);
static unsigned ntp=0,sockets=0,vpn=0,mqtt=0,web=0,mdns=0;
static struct { bool wgEnable=true; } vpnCfg;
static struct { bool enable=true; } mqttCfg;
static struct { bool apStarted=false,connectedEther=false; char deviceId[32]={}; } vars;
static struct { bool wifiEnable=true,ethEnable=true; } networkCfg;
static struct { int workMode=WORK_MODE_NETWORK; bool disableWeb=false; char hostname[32]={}; } systemCfg;
static struct { struct { int mode; } modeLED,powerLED; } ledControl;
static struct { int state() { return STOPPED; } void start() {} } tmrNetworkOverseer;
static bool lanStarted=false,ap=false,updWeb=false;
static void NetworkEvent(int) {}
static struct {
    void onEvent(void (*)(int)) {}
    void mode(int) {}
    void disconnect() {}
    void softAPdisconnect(bool) { ap=false; }
    void softAPConfig(IPAddress,IPAddress,IPAddress) {}
    void softAP(const char *id) { assert(!strcmp(id,"CZC-test")); ap=true; }
    void setSleep(bool) {}
} WiFi;
enum class DNSReplyCode { NoError };
static struct {
    void stop() {}
    void setErrorReplyCode(DNSReplyCode) {}
    void start(int,const char *,IPAddress) {}
} dnsServer;
static void setClock(void *) {}
static void xTaskCreate(void (*fn)(void *),const char *,unsigned,void *,unsigned,void *) {
    assert(fn==setClock); ++ntp;
}
static void startSocketServer() { ++sockets; }
static void wgBegin() { ++vpn; }
static void mqttConnectSetup() { assert(systemCfg.hostname[0]); ++mqtt; }
static void initWebServer() { ++web; }
static void mDNS_start() { assert(!strcmp(systemCfg.hostname,"CZC-test")); ++mdns; }
static void initLan() { lanStarted=true; }
static void writeDefaultDeviceID(char *id) { assert(lanStarted); strcpy(id,"CZC-test"); }
template<class S,class V,class M> static void writeDeviceId(S &cfg,V &,M &) { strcpy(cfg.hostname,vars.deviceId); }
static void delay(unsigned) {}
static void usbModeSet(int) {}
static bool backhaulEnabled() { return false; }
void startAP(bool);
static void connectWifi() { startAP(true); } // Missing SSID while Ethernet is waiting for DHCP.
#include "../../.backhaul-tests/services-under-test.inc"

int main(int argc,char **argv) {
    bool usb=argc>1 && !strcmp(argv[1],"usb");
    systemCfg.workMode=usb?WORK_MODE_USB:WORK_MODE_NETWORK;
    setupCoordinatorMode();
    assert(vars.apStarted && ap && updWeb);
    assert(ntp==1 && sockets==unsigned(!usb) && vpn==1 && mqtt==1 && web==1 && mdns==1);
    // Link recovery closes the fallback AP, subsequent link loss reopens it.
    // Neither event must register cron again or replace live services.
    for(unsigned attempt=0;attempt<10;attempt++) {
        startAP(false);
        assert(!vars.apStarted);
        startAP(true);
        assert(vars.apStarted && ap);
    }
    assert(ntp==1 && sockets==unsigned(!usb) && vpn==1 && mqtt==1 && web==1 && mdns==1);
    puts("PASS service startup: initialized hostname, early AP in network/USB mode, one set of services across repeated fallback/recovery");
}
