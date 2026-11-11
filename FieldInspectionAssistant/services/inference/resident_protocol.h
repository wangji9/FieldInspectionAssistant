#pragma once
// Local JSON-lines control for the pinned Rockchip demos. Models stay loaded;
// every inference owns fresh input buffers and starts without conversation KV.
#include <QJsonDocument>
#include <QJsonObject>
#include <atomic>
#include <csignal>
#include <functional>
#include <iostream>
#include <thread>
#include <chrono>

static std::atomic<bool> fieldops_cancelled{false};
static void fieldops_signal(int) { fieldops_cancelled.store(true); }
static void resident_event(QJsonObject obj) {
    auto data=QJsonDocument(obj).toJson(QJsonDocument::Compact);
    printf("\n@@RESIDENT@@%s\n",data.constData());fflush(stdout);
}
static QJsonObject resident_memory(rknn3_context context) {
    rknn3_dev_mem_info mem{};
    QJsonObject result;
    if (rknn3_query(context,RKNN3_QUERY_DEVICE_MEM_INFO,&mem,sizeof(mem))==0 && mem.node_num) {
        uint64_t total=0,free=0;
        for (unsigned i=0;i<mem.node_num;++i) {
            total+=mem.node_mem_info[i].total;free+=mem.node_mem_info[i].free;
        }
        result["device_total_mb"]=double(total)/1048576;
        result["device_free_mb"]=double(free)/1048576;
    }
    return result;
}
struct ResidentCleanup {
    std::function<void()> fn;
    ~ResidentCleanup() { fn(); }
};
static void resident_loop(double load_ms,rknn3_session *session,rknn3_context context,
                          const std::function<int(const QJsonObject&)> &run) {
    signal(SIGUSR1,fieldops_signal);
    std::atomic<bool> active{false}, exiting{false};
    std::thread stopper([&]{
        bool stopped=false;
        while (!exiting.load()) {
            if (!active.load() || !fieldops_cancelled.load()) stopped=false;
            if (active.load() && fieldops_cancelled.load() && !stopped) {
                rknn3_session_stop(session);stopped=true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    QJsonObject ready=resident_memory(context);ready["event"]="ready";ready["load_ms"]=load_ms;
    resident_event(ready);
    std::string line;
    while (std::getline(std::cin,line)) {
        QJsonParseError error;
        auto doc=QJsonDocument::fromJson(QByteArray::fromStdString(line),&error);
        auto input=doc.object();
        if (error.error!=QJsonParseError::NoError || !doc.isObject()) {
            resident_event({{"event","done"},{"code",-1},{"error","Invalid JSON request"}});continue;
        }
        auto id=input["id"].toString();
        if (input["op"]=="memory") {
            auto value=resident_memory(context);value["event"]="done";value["id"]=id;value["code"]=0;
            resident_event(value);continue;
        }
        fieldops_cancelled.store(false);
        int limit=qBound(8,input["max_tokens"].toInt(256),4096);
        auto value=QByteArray::number(limit);setenv("BOARD_AI_MAX_TOKENS",value.constData(),1);
        rknn3_session_clear_kvcache(session,RKNN3_KVCACHE_CLEAR_ALL);
        active.store(true);
        int ret=run(input);
        active.store(false);
        resident_event({{"event","done"},{"id",id},{"code",ret},{"cancelled",fieldops_cancelled.load()}});
    }
    exiting.store(true);stopper.join();
}
