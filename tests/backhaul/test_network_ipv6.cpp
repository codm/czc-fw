#include <cassert>
#include <cstring>
#include <cstdio>
#include <arpa/inet.h>
#include "../../src/network_ipv6.h"

static esp_netif_t device{};
static unsigned creates=0,reads=0;
static bool transientFailure=false,disappear=false;
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *) {return disappear ? nullptr : &device;}
bool esp_netif_is_netif_up(esp_netif_t *n) {return n->up;}
int esp_netif_get_ip_info(esp_netif_t *n,esp_netif_ip_info_t *out) {*out=n->info;return n->fail ? -1 : ESP_OK;}
int esp_netif_get_all_ip6(esp_netif_t *n,esp_ip6_addr_t *out) {++reads;memcpy(out,n->ipv6,sizeof(n->ipv6));return n->count;}
esp_ip6_addr_type_t esp_netif_ip6_get_addr_type(esp_ip6_addr_t *ip) {
    auto *b=reinterpret_cast<unsigned char *>(ip->addr);
    return b[0]==0xfe && (b[1]&0xc0)==0x80 ? ESP_IP6_ADDR_IS_LINK_LOCAL : ESP_IP6_ADDR_IS_GLOBAL;
}
// SDK v4.4.5 esp_netif_create_ip6_linklocal_api requires netif_is_up;
// https://github.com/espressif/esp-idf/blob/v4.4.5/components/esp_netif/lwip/esp_netif_lwip.c
int esp_netif_create_ip6_linklocal(esp_netif_t *n) {
    ++creates;
    if(!n->up || transientFailure) return -1;
    inet_pton(AF_INET6,"fe80::1234",n->ipv6[0].addr);n->count=1;return ESP_OK;
}
int main() {
    NetworkIpv6 wifi;
    // Neither association without IPv4 nor an enabled but disconnected
    // interface may trigger IPv6 initialization.
    for(uint32_t t=0;t<10000;++t) wifi.ensure("WIFI_STA_DEF",true,t);
    device.up=true;
    for(uint32_t t=10000;t<20000;++t) wifi.ensure("WIFI_STA_DEF",true,t);
    device.info.ip.addr=UINT32_MAX;
    for(uint32_t t=20000;t<30000;++t) wifi.ensure("WIFI_STA_DEF",true,t);
    assert(creates==0 && reads==0);
    device.info.ip.addr=1;
    for(uint32_t t=20000;t<30000;++t) wifi.ensure("WIFI_STA_DEF",false,t);
    assert(creates==0 && reads==0);
    // Wait 5 seconds after IPv4; do not reset DAD after successful creation.
    for(uint32_t t=30000;t<35000;++t) assert(!wifi.ensure("WIFI_STA_DEF",true,t));
    assert(creates==0);
    assert(wifi.ensure("WIFI_STA_DEF",true,35000));
    assert(wifi.last==ESP_OK && creates==1);
    inet_pton(AF_INET6,"2001:db8::1234",device.ipv6[1].addr);device.count=2;
    auto saved=device;
    for(uint32_t t=35001;t<80000;++t) assert(!wifi.ensure("WIFI_STA_DEF",true,t));
    assert(creates==1 && reads==9 && !memcmp(saved.ipv6,device.ipv6,sizeof(device.ipv6)));
    // Reconnect with addresses retained: keep both tentative LL and global.
    assert(!wifi.ensure("WIFI_STA_DEF",false,80000));
    for(uint32_t t=80001;t<100000;++t) assert(!wifi.ensure("WIFI_STA_DEF",true,t));
    assert(creates==1 && reads==12 && !memcmp(saved.ipv6,device.ipv6,sizeof(device.ipv6)));
    // Lost link/IPv4 resets the settle period. No background churn on failure:
    // at most three consecutive initialization calls while the address is absent.
    device.up=false;assert(!wifi.ensure("WIFI_STA_DEF",true,100000));
    device.up=true;device.count=0;memset(device.ipv6,0,sizeof(device.ipv6));
    transientFailure=true;
    for(uint32_t t=100001;t<200000;++t) wifi.ensure("WIFI_STA_DEF",true,t);
    assert(creates==4 && wifi.last==-1);
    // A new connection can recover after the earlier failures.
    disappear=true;assert(!wifi.ensure("WIFI_STA_DEF",true,200000));disappear=false;
    transientFailure=false;
    assert(!wifi.ensure("WIFI_STA_DEF",true,200001));
    assert(wifi.ensure("WIFI_STA_DEF",true,205001));assert(creates==5);
    // Changing IPv4 also requires a new settle period.
    device.info.ip.addr=2;device.count=0;
    assert(!wifi.ensure("WIFI_STA_DEF",true,210000));
    assert(!wifi.ensure("WIFI_STA_DEF",true,214999));
    assert(wifi.ensure("WIFI_STA_DEF",true,215000));assert(creates==6);
    device.fail=true;assert(!wifi.ensure("WIFI_STA_DEF",true,220000));device.fail=false;
    NetworkIpv6 wrap;device.count=0;
    assert(!wrap.ensure("WIFI_STA_DEF",true,0xfffffff0));
    assert(!wrap.ensure("WIFI_STA_DEF",true,100));
    assert(wrap.ensure("WIFI_STA_DEF",true,5000));
    // Ethernet can lose its address table while link and IPv4 stay unchanged.
    // Recover the link-local address without a reboot or an artificial link flap.
    NetworkIpv6 ethernet;
    device=esp_netif_t{};device.up=true;device.info.ip.addr=1;
    inet_pton(AF_INET6,"fe80::1234",device.ipv6[0].addr);
    inet_pton(AF_INET6,"2001:db8:1::1234",device.ipv6[1].addr);device.count=2;
    assert(!ethernet.ensure("ETH_DEF",true,0));
    assert(!ethernet.ensure("ETH_DEF",true,5000));
    unsigned before=creates;
    device.count=0;memset(device.ipv6,0,sizeof(device.ipv6));
    assert(!ethernet.ensure("ETH_DEF",true,9999));
    assert(ethernet.ensure("ETH_DEF",true,10000));assert(creates==before+1);
    // A new advertised prefix does not justify restarting DAD or clearing LL.
    inet_pton(AF_INET6,"2001:db8:2::1234",device.ipv6[1].addr);device.count=2;
    saved=device;
    for(uint32_t t=10001;t<30000;++t) assert(!ethernet.ensure("ETH_DEF",true,t));
    assert(creates==before+1 && !memcmp(saved.ipv6,device.ipv6,sizeof(device.ipv6)));
    // Another loss on the same connection can recover after a successful one.
    device.count=0;memset(device.ipv6,0,sizeof(device.ipv6));
    assert(ethernet.ensure("ETH_DEF",true,30000));assert(creates==before+2);
    puts("PASS IPv6 lifecycle: 5 s settle/polls, retained DAD/global addresses, bounded failures, reconnect, IP change, missing interface, wrap, address loss with link up, prefix change and repeated recovery");
}
