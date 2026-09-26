#include <Znp.h>
#include <Settings.h>
#include <Recovery.h>
#include <RadioImage.h>
#include "../../src/backhaul_random.h"
#include <cassert>
#include <cstdio>
#include <thread>

SerialFixture Serial2;
bool fixtureEntropyEnabled=false;
thread_local FixtureTask *fixtureCurrentTask=nullptr;
static void settled() { std::this_thread::sleep_for(std::chrono::milliseconds(15)); }
static void respond(const uint8_t *p,size_t) {
    ZnpFrame reply; uint8_t result[]={0,p[3]}; reply.build((p[2]&31)|0x60,p[3],result,sizeof(result)); Serial2.feed(reply.data,reply.size);
}
int main() {
    czc_random::begin(); assert(czc_random::ready && !fixtureEntropyEnabled);
    uint8_t randomA[32],randomB[32];
    assert(!czc_random::fill(nullptr,randomA,sizeof(randomA)));
    assert(!czc_random::fill(nullptr,randomB,sizeof(randomB)));
    assert(memcmp(randomA,randomB,sizeof(randomA))!=0);
    // Forced reseed fails closed after the startup entropy source is released.
    mbedtls_ctr_drbg_set_reseed_interval(&czc_random::drbg,0);
    assert(czc_random::fill(nullptr,randomA,sizeof(randomA))!=0);
    mbedtls_ctr_drbg_free(&czc_random::drbg); vSemaphoreDelete(czc_random::mutex);
    czc::Settings settings;
    assert(!czc::validate(settings,6638));
    // Reserved NVS bytes do not select build capabilities.
    settings.legacyDiagnostics=255; settings.legacyAdminPort=1;
    assert(!czc::validate(settings,6638));
    czc::Settings migrated; settings.key[0]=0x42;
    memcpy(static_cast<void *>(&migrated),&settings,180);
    assert(migrated.key[0]==0x42 && migrated.legacyAdminPort==1 && !migrated.legacyDiagnostics);
    // Repeated UART faults cannot produce a restart loop; deadlines survive millis wrap.
    czc::Recovery recovery;
    uint32_t t=0xfffffff0;
    assert(!recovery.due(t,true,false));
    for(unsigned i=0;i<3;i++) {
        uint32_t delay=5000u<<i;
        assert(!recovery.due(t+delay-1,true,false));
        assert(recovery.due(t+delay,true,false)); t+=delay; recovery.attempted(t);
        assert(!recovery.due(t+1,false,false)); assert(!recovery.due(t+1,true,false)); ++t;
    }
    assert(recovery.total==3 && !recovery.due(t+1000000,true,false));
    assert(!recovery.due(t+1000001,false,true));
    assert(!recovery.due(t+1060000,false,true) && recovery.attempts==3);
    assert(!recovery.due(t+1060001,false,true) && recovery.attempts==0);
    settings.mode=1; assert(czc::validate(settings,6638));
    for(uint8_t cmd=0xc7;cmd<=0xcb;cmd++) assert(czc::privateRadioCommand(cmd));
    assert(!czc::privateRadioCommand(0xc0) && !czc::privateRadioCommand(0xc6));
    settings.hasKey=1;
    assert(!czc::validate(settings,6638)); // Master needs only its mode and PSK, no peer IEEE.
    assert(czc::decodeHex("0x00124b002e11424d",settings.peerIEEE,8,true));
    assert(!czc::validate(settings,6638));
    assert(!czc::decodeHex("00124g002e11424d",settings.peerIEEE,8,true));
    settings.mode=2; assert(czc::validate(settings,6638));
    strcpy(settings.host,"fd12::144"); assert(!czc::validate(settings,6638));
    settings.legacyIpv6=0; settings.legacyRawTcp=0; assert(!czc::validate(settings,6638));
    strcpy(settings.host,"fe80::144%eth"); assert(!czc::validate(settings,6638));
    strcpy(settings.host,"http://192.168.8.144"); assert(czc::validate(settings,6638));
    strcpy(settings.host,"czc-master.local"); settings.peerPort=6638; assert(czc::validate(settings,6638));
    settings.peerPort=7444; assert(bool(czc::validate(settings,6638))==czc::debugBuild);
    settings.peerPort=7443; assert(!czc::validate(settings,6638));
    memset(settings.host,'x',sizeof(settings.host)); assert(czc::validate(settings,6638));
    uint8_t image[]={0,0,2,0x20,1,1,0,0};
    assert(czc::radioImageHeader(image,8,720896,720896));
    assert(!czc::radioImageHeader(image,8,1200000,720896));
    assert(!czc::radioImageHeader(image,7,720896,720896));
    image[0]=0xe9; assert(!czc::radioImageHeader(image,8,720896,720896));
    image[0]=0; image[4]=0; assert(!czc::radioImageHeader(image,8,720896,720896));

    Radio radio; assert(radio.begin()); Serial2.onWrite=respond;
    // The radio can return a valid C2 SRSP after the old 350 ms budget.
    // Keep that response with its request; do not retry it or reset the radio.
    std::thread delayed;
    Serial2.onWrite=[&](const uint8_t *p,size_t n) {
        ZnpFrame request; memcpy(request.data,p,n); request.size=n;
        delayed=std::thread([request] {
            std::this_thread::sleep_for(std::chrono::milliseconds(421));
            respond(request.data,request.size);
        });
    };
    ZnpFrame delayedReply;
    unsigned beforeWrites=Serial2.writes;
    bool received=radio.command(0xc2,nullptr,0,delayedReply);
    delayed.join();
    assert(received && !radio.fatal && !radio.timeouts);
    assert(delayedReply.data[5]==0xc2 && Serial2.writes==beforeWrites+1 && radio.replyMs>=421);
    Serial2.onWrite=respond;
    // Concurrent private commands and controller SREQs must receive their own SRSP.
    auto call=[&](uint8_t cmd) { for(unsigned i=0;i<20;i++) { ZnpFrame reply; assert(radio.command(cmd,nullptr,0,reply)); assert(reply.data[5]==cmd); } };
    std::thread peer(call,0xc6),controller(call,2); peer.join(); controller.join();
    assert(!radio.fatal);
    // A valid SRSP following an overrun does not make the damaged stream safe.
    // The lost bytes could have belonged to an AF indication before that SRSP.
    Serial2.onWrite=[](const uint8_t *p,size_t n) { Serial2.error(UART_FIFO_OVF_ERROR); respond(p,n); };
    ZnpFrame hardwareReply;
    assert(!radio.command(0xc2,nullptr,0,hardwareReply));
    assert(radio.fatal && !strcmp(radio.error,"rx_fifo_overflow"));
    unsigned faultWrites=Serial2.writes;
    assert(!radio.command(0xc2,nullptr,0,hardwareReply) && Serial2.writes==faultWrites);
    assert(radio.suspend()); radio.resume(); Serial2.onWrite=respond;
    assert(!radio.rxError && radio.command(0xc2,nullptr,0,hardwareReply));

    // Exercise packets larger than the hardware FIFO, with embedded SOFs and
    // arbitrary read boundaries, interleaved with ordinary asynchronous events.
    Serial2.onWrite=[](const uint8_t *p,size_t) {
        uint8_t payload[148]; for(unsigned i=0;i<sizeof(payload);i++) payload[i]=i%3 ? i : 0xfe;
        payload[0]=0;
        ZnpFrame event,response; event.build(0x45,0xc0,payload,8);
        response.build((p[2]&31)|0x60,p[3],payload,sizeof(payload));
        Serial2.feed(event.data,event.size);
        for(unsigned offset=0;offset<response.size;) {
            unsigned n=std::min<unsigned>(1+offset%31,response.size-offset);
            Serial2.feed(response.data+offset,n); offset+=n;
        }
    };
    for(unsigned i=0;i<20;i++) {
        assert(radio.command(0xc2,nullptr,0,hardwareReply));
        assert(hardwareReply.size==153 && hardwareReply.data[7]==0xfe);
    }
    Serial2.onWrite=respond;
    assert(radio.suspend());
    unsigned reads=Serial2.reads,writes=Serial2.writes;
    const uint8_t bsl[]={0xcc,0x33,0x00,0x55}; Serial2.feed(bsl,sizeof(bsl)); settled();
    Serial2.error(UART_FRAME_ERROR); assert(!radio.rxError);
    assert(Serial2.reads==reads); // CCTools exclusively owns BSL bytes during maintenance.
    ZnpFrame reply; assert(!radio.command(2,nullptr,0,reply)); assert(Serial2.writes==writes);
    assert(Serial2.read()==0xcc);
    radio.resume(); assert(radio.command(2,nullptr,0,reply)); assert(!radio.fatal);
    // A timed-out request must not donate its late SRSP to a future controller.
    Serial2.onWrite=nullptr;
    assert(!radio.command(2,nullptr,0,reply)); assert(radio.fatal && radio.timeouts==1);
    assert(radio.timedOut && radio.timeoutWait>=750 && radio.timeoutRx==0 && radio.timeoutFrames==0);
    assert(!radio.timeoutPartial && radio.replyMs==UINT32_MAX);
    writes=Serial2.writes;
    uint8_t ok=0; ZnpFrame late; late.build(0x61,2,&ok,1); Serial2.feed(late.data,late.size);
    settled(); assert(radio.replyMs>=radio.timeoutWait && radio.replyMs!=UINT32_MAX);
    assert(radio.fatal && !strcmp(radio.error,"srsp_timeout") && Serial2.writes==writes);
    assert(!radio.timeoutRx); // Snapshot stays at the deadline, not the late arrival.
    assert(!radio.command(2,nullptr,0,reply));
    assert(radio.suspend()); radio.resume(); Serial2.onWrite=respond;
    assert(!radio.timedOut && radio.replyMs==UINT32_MAX);
    assert(radio.command(2,nullptr,0,reply) && reply.data[5]==2);
    // Distinguish an incomplete reply from silence, then observe its late tail.
    ZnpFrame split; split.build(0x61,0xc2,&ok,1);
    Serial2.onWrite=[&](const uint8_t *,size_t) { Serial2.feed(split.data,4); };
    assert(!radio.command(0xc2,nullptr,0,reply));
    assert(radio.timeoutRx==4 && !radio.timeoutFrames && radio.timeoutPartial==(4|(6<<8)));
    Serial2.feed(split.data+4,split.size-4); settled();
    assert(radio.replyMs!=UINT32_MAX && radio.fatal && radio.timeoutPartial==(4|(6<<8)));
    assert(radio.suspend()); radio.resume();
    // Valid AREQs during a missing SRSP prove the receiver is still processing.
    ZnpFrame unsolicited; unsolicited.build(0x45,0xc0,&ok,1);
    Serial2.onWrite=[&](const uint8_t *,size_t) { Serial2.feed(unsolicited.data,unsolicited.size); };
    assert(!radio.command(0xc2,nullptr,0,reply));
    assert(radio.timeoutRx==unsolicited.size && radio.timeoutFrames==1 && !radio.timeoutPartial);
    assert(radio.replyMs==UINT32_MAX);
    assert(radio.suspend()); radio.resume(); Serial2.onWrite=respond;
    // A blocked receiver is different from a radio that sends no bytes.
    Serial2.pauseReads=true;
    for(unsigned i=0;i<100 && !Serial2.readPaused;i++) settled();
    assert(Serial2.readPaused);
    assert(!radio.command(0xc2,nullptr,0,reply));
    assert(radio.timeoutRx==0 && radio.timeoutGap>=radio.timeoutWait && radio.replyMs==UINT32_MAX);
    Serial2.pauseReads=false; settled();
    assert(radio.replyMs!=UINT32_MAX && radio.fatal);
    assert(radio.suspend()); radio.resume();
    // Malformed RX stops further RPC; BSL/restart can recover it.
    ZnpFrame bad; bad.build(0x44,0x81,&ok,1); bad.data[bad.size-1]^=1; Serial2.feed(bad.data,bad.size); settled();
    assert(radio.fatal && !strcmp(radio.error,"frame_fcs") && radio.errorCmd0==0x44 && radio.errorCmd1==0x81);
    assert(radio.suspend()); radio.resume(); assert(!radio.fatal);
    const uint8_t invalidLength[]={0xfe,251}; Serial2.feed(invalidLength,sizeof(invalidLength)); settled();
    assert(radio.fatal && !strcmp(radio.error,"frame_length"));
    assert(radio.suspend()); radio.resume();
    // A planned reset can produce a break; its real resetInd releases the barrier.
    radio.expectReset(); Serial2.error(UART_BREAK_ERROR);
    uint8_t resetBody[6]{}; ZnpFrame reset; reset.build(0x41,0x80,resetBody,sizeof(resetBody));
    Serial2.feed(reset.data,reset.size); settled();
    assert(!radio.resetWaiting && !radio.fatal && !radio.rxError);
    assert(radio.command(0xc2,nullptr,0,reply));
    // Controller overflow is reported, not silently treated as successful AF delivery.
    radio.adminActive=true;
    ZnpFrame event; event.build(0x44,0x81,&ok,1);
    for(unsigned i=0;i<26;i++) Serial2.feed(event.data,event.size);
    settled(); assert(radio.adminOverflow && radio.overflows==2);
    assert(radio.suspend()); radio.resume(); assert(!radio.adminOverflow);
    // An unsolicited SRSP is also fatal; ordinary asynchronous events are not.
    Serial2.feed(late.data,late.size); settled(); assert(radio.fatal);
    radio.stop();
    puts("PASS integrated UART: concurrent RPC, fragmented large frames, FIFO loss, FCS/length faults, silence/partial/AREQ timeout snapshots, late SRSP without replay, exclusive BSL, reset, overflow and recovery; settings, radio image and entropy");
}
