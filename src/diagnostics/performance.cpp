#include "diagnostics/performance.h"
#if PIUU_PERFORMANCE_DIAGNOSTICS
#include "hardware/app_log.h"
#include "diagnostics/metrics.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <cstring>
#include "input/ears_link.h"
#include "config/ears_config.h"
#include "config/app_config.h"
#include "audio/audio_downlink.h"

namespace Diagnostics {
namespace {
portMUX_TYPE timingLock=portMUX_INITIALIZER_UNLOCKED;
Timing timing[2];
constexpr unsigned MAX_TASKS=48;
// Static storage avoids allocating diagnostics on the heap or loop's stack.
#if configUSE_TRACE_FACILITY && configGENERATE_RUN_TIME_STATS
TaskStatus_t tasks[MAX_TASKS];
struct Previous { TaskHandle_t handle; UBaseType_t number; uint32_t runtime; };
Previous previous[MAX_TASKS];
struct Report { char name[configMAX_TASK_NAME_LEN]; uint32_t stack,percent; int core; bool measured; };
Report reports[MAX_TASKS];
unsigned previousCount=0, reportCount=0, reportNext=0;
uint32_t previousTime=0;
bool haveBaseline=false;
uint32_t captureTasks() {
    configRUN_TIME_COUNTER_TYPE total=0;
    const uint32_t begin=uint32_t(esp_timer_get_time());
    const unsigned count=uxTaskGetSystemState(tasks,MAX_TASKS,&total);
    const uint32_t captureUs=uint32_t(esp_timer_get_time())-begin;
    if (!count) {
        haveBaseline=false; previousCount=reportCount=reportNext=0;
        appLog().println("[CPU] snapshot unavailable (task capacity=48); next sample is baseline");
        return captureUs;
    }
    const uint32_t wall=uint32_t(total)-previousTime;
    int busy[2]={-1,-1};
    for (unsigned i=0;i<count;++i) {
        const auto &task=tasks[i];auto &report=reports[i];
        // Copy while in loop; app workers are long-lived. Do not retain name pointers.
        strncpy(report.name,task.pcTaskName,sizeof(report.name)-1);
        report.name[sizeof(report.name)-1]=0;
        report.stack=task.usStackHighWaterMark;report.measured=false;report.percent=0;
#if configTASKLIST_INCLUDE_COREID
        report.core=task.xCoreID;
        if (report.core<0 || report.core>=portNUM_PROCESSORS) report.core=-1;
#else
        report.core=-1;
#endif
        if (haveBaseline && wall) for (unsigned j=0;j<previousCount;++j) {
            if (task.xHandle==previous[j].handle && task.xTaskNumber==previous[j].number) {
                report.percent=percentTenths(uint32_t(task.ulRunTimeCounter)-previous[j].runtime,wall);
                report.measured=true;break;
            }
        }
        for (int core=0;core<2;++core) {
            if (report.measured && task.xHandle==xTaskGetIdleTaskHandleForCore(core))
                busy[core]=1000-int(report.percent);
        }
    }
    for (unsigned i=0;i<count;++i)
        previous[i]={tasks[i].xHandle,tasks[i].xTaskNumber,uint32_t(tasks[i].ulRunTimeCounter)};
    previousCount=count;previousTime=uint32_t(total);haveBaseline=true;
    reportCount=count;reportNext=0;
    if (busy[0]>=0 && busy[1]>=0)
        appLog().printf("[CPU t=%lu] core0=%d.%d%% core1=%d.%d%% tasks=%u snapshot_us=%lu\n",
            (unsigned long)millis(),busy[0]/10,busy[0]%10,busy[1]/10,busy[1]%10,count,(unsigned long)captureUs);
    else appLog().printf("[CPU t=%lu] baseline tasks=%u snapshot_us=%lu\n",(unsigned long)millis(),count,(unsigned long)captureUs);
    return captureUs;
}
#endif
}
Measure::Measure(Operation operation):operation_(operation),start_(uint32_t(esp_timer_get_time())) {}
Measure::~Measure() {
    const uint32_t elapsed=uint32_t(esp_timer_get_time())-start_;
    portENTER_CRITICAL(&timingLock);
    timing[unsigned(operation_)].add(elapsed);
    portEXIT_CRITICAL(&timingLock);
}
void performancePoll() {
    static uint32_t lastSnapshot=0,lastLine=0;
    static Timing pending[2];
    static unsigned pendingNext=3;
    const uint32_t now=millis();
    if (uint32_t(now-lastSnapshot)>=5000) {
        lastSnapshot=now;
        portENTER_CRITICAL(&timingLock);
        pending[0]=timing[0];pending[1]=timing[1];timing[0]=Timing{};timing[1]=Timing{};
        portEXIT_CRITICAL(&timingLock);
        pendingNext=0;
#if configUSE_TRACE_FACILITY && configGENERATE_RUN_TIME_STATS
        captureTasks();
#else
        appLog().println("[CPU] unavailable: SDK runtime stats disabled");
#endif
        lastLine=now;return;
    }
    // At most one line per 80 ms, outside every lock and audio worker.
    if (uint32_t(now-lastLine)<80) return;
    lastLine=now;
    if (pendingNext<2) {
        const unsigned index=pendingNext++;const auto &value=pending[index];
        appLog().printf("[PERF t=%lu] op=%s n=%lu avg_us=%lu max_us=%lu over20ms=%lu\n",
            (unsigned long)now,index==0?"opus":"i2s_wait_write",(unsigned long)value.count,
            (unsigned long)value.meanUs(),(unsigned long)value.maxUs,(unsigned long)value.overBudget);
        return;
    }
    if (pendingNext==2) {
        ++pendingNext;
        const auto ears=earsLinkQueueStats();
        AudioBufferStats buffer{};
        const bool valid=audioDownlinkBufferStats(buffer);
        appLog().printf("[BUFFERS t=%lu] ears=%lu/%u peak=%lu events=%lu/%u peak=%lu jitter=%lu/%u peak=%lu valid=%d rebuf_n=%lu total_ms=%lu max_ms=%lu active_ms=%lu\n",
            (unsigned long)now,(unsigned long)ears.audio,unsigned(EarsConfig::AUDIO_QUEUE_FRAMES),(unsigned long)ears.audioPeak,
            (unsigned long)ears.events,unsigned(EarsConfig::EVENT_QUEUE_ITEMS),(unsigned long)ears.eventPeak,
            (unsigned long)buffer.buffered,unsigned(JITTER_SLOTS),(unsigned long)buffer.peak,valid,
            (unsigned long)buffer.rebufferCount,(unsigned long)buffer.totalMs,(unsigned long)buffer.maxMs,(unsigned long)buffer.activeMs);
        return;
    }
#if configUSE_TRACE_FACILITY && configGENERATE_RUN_TIME_STATS
    if (reportNext<reportCount) {
        const auto &report=reports[reportNext++];
        appLog().printf("[TASK t=%lu] name=%s affinity=%d cpu=%lu.%lu%% valid=%d stack_min_bytes=%lu\n",
            (unsigned long)lastSnapshot,report.name,report.core,(unsigned long)(report.percent/10),
            (unsigned long)(report.percent%10),report.measured,(unsigned long)report.stack);
    }
#endif
}
}
#endif
