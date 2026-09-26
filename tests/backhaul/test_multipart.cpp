// The actual pinned WebServer parser driving the actual ESP upload callbacks.
#define CZC_TEST_MULTIPART
#define main update_callback_tests
#include "test_update.cpp"
#undef main

#define F(x) x
#define FPSTR(x) x
#define log_v(...) ((void)0)
#define log_e(...) ((void)0)
using boolean=bool;
static constexpr size_t HTTP_UPLOAD_BUFLEN=1436;
static constexpr int WEBSERVER_MAX_POST_ARGS=32;
static const char *Content_Type="Content-Type",*filename="filename";
namespace mime { enum {txt}; struct {const char *mimeType;} mimeTable[]={{"text/plain"}}; }
static unsigned ends=0,aborts=0;
struct Handler {
    bool canUpload(const String &) { return true; }
    void upload(WebServer &,const String &,HTTPUpload &part) {
        ends+=part.status==UPLOAD_FILE_END;
        aborts+=part.status==UPLOAD_FILE_ABORTED;
        handleEspUpdateUpload();
    }
} handler;
#include "../../.backhaul-tests/multipart-under-test.inc"

static std::string form(const std::string &tail="--\r\n") {
    return "--czc-boundary\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"app.bin\"\r\n"
           "Content-Type: application/octet-stream\r\n\r\n"+
           std::string(f.body.begin(),f.body.end())+"\r\n--czc-boundary"+tail;
}
static bool parse(const std::string &request,bool stalled=false) {
    serverWeb._currentHandler=&handler;
    WiFiClient client(request); client.keepOpen=stalled;
    return serverWeb._parseForm(client,"czc-boundary",request.size());
}
static void fresh() { reset(); ends=aborts=0; }
static void failed() {
    assert(!f.ended && !f.locks && !espUploadHeld && !espUploadStarted && !espUpdateRestartAt);
    assert(aborts==1 && f.aborted);
    f.time+=10000; espUpdateLoop(); assert(!f.restarts);
}
int main() {
    // Complete upload, including an exact closing line: commit only afterwards.
    fresh(); assert(parse(form())); assert(ends==1 && !aborts && !f.ended);
    finish("esp_updated",200); success();

    // A disconnect/stall in every byte of the final boundary/suffix must return,
    // abort the flash and release maintenance. No power cycle is needed to retry.
    for(bool stalled:{false,true}) for(size_t missing=1;missing<=24;missing++) {
        fresh(); auto request=form(); unsigned started=f.time;
        assert(!parse(request.substr(0,request.size()-missing),stalled));
        assert(f.time-started<=42); failed();
        serverWeb.clearParser(); f.written.clear(); f.aborted=false;
        assert(parse(form())); finish("esp_updated",200); success();
    }
    // A completed file followed by an incomplete next form part must also abort.
    fresh(); assert(!parse(form("\r\nContent-Disposition: form-data; name=\"extra\"\r\n\r\ntruncated")));
    assert(ends==1); failed();
    fresh(); assert(!parse(form("invalid\r\n"))); assert(!ends); failed();
    fresh(); assert(!parse(form().substr(0,40),true)); assert(!f.starts && !f.locks && !f.ended);
    fresh(); f.authorized=false; assert(parse(form())); handleUpdateRequest();
    assert(serverWeb.responseStatus==401 && !f.starts && !f.locks && !f.restarts);
    serverWeb.clearParser();
    puts("PASS actual HTTP multipart + ESP OTA: final-boundary disconnect/stall, incomplete next part, rejected malformed suffix, retry, authorization, commit and delayed reboot");
}
