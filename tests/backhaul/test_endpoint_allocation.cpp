#include <Backhaul.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

static bool failAllocation=false;
void *operator new(std::size_t size,const std::nothrow_t &) noexcept {
    return failAllocation ? nullptr : std::malloc(size);
}
void operator delete(void *p) noexcept { std::free(p); }
static uint32_t bootEpoch=7,connectionEpoch=0,reconnects=0;
static struct { int mode=2; } cfg;
static uint64_t nowMs() { return 100; }
struct Peer {
    backhaul::Endpoint *endpoint=nullptr;
    uint8_t remote[32]{},session[8]{};
    char remoteIp[64]{};
    bool sessionOpen=false;
    struct {
        bool ready=true;
        void peerAddress(char *out,size_t) { strcpy(out,"192.0.2.1"); }
    } tls;
};
static void disconnectPeer(Peer &p,const char *reason) {
    assert(!strcmp(reason,"memory_or_epoch"));
    p.tls.ready=false; delete p.endpoint; p.endpoint=nullptr;
}
#include "../../.backhaul-tests/endpoint-open-under-test.inc"

int main() {
    Peer p; failAllocation=true;
    openEndpoint(p); assert(!p.endpoint && !p.sessionOpen && !p.tls.ready && !reconnects);
    failAllocation=false; p.tls.ready=true;
    openEndpoint(p); assert(p.endpoint && p.sessionOpen && p.tls.ready && reconnects==1);
    delete p.endpoint;
    puts("PASS peer allocation: exhausted heap rejects one session without throwing; retry succeeds after memory recovery");
}
