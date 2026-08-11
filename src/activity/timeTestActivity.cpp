#include "timeTestActivity.h"

static timeTestActivity* mActivityPtr;

REGISTER_ACTIVITY(timeTestActivity);

typedef struct {
    int id;
    int time;
} S_ACTIVITY_TIMEER;

#include "logic/timeTestLogic.cc"

timeTestActivity::timeTestActivity() {
}

timeTestActivity::~timeTestActivity() {
    unregisterProtocolDataUpdateListener(onProtocolDataUpdate);
    onUI_quit();
    mActivityPtr = NULL;
}

const char* timeTestActivity::getAppName() const{
    return "timeTest.ftu";
}

void timeTestActivity::onCreate() {
    Activity::onCreate();
    mActivityPtr = this;
    onUI_init();
    registerProtocolDataUpdateListener(onProtocolDataUpdate);
    rigesterActivityTimer();
}

void timeTestActivity::onResume() {
    Activity::onResume();
    onUI_show();
}

void timeTestActivity::onPause() {
    Activity::onPause();
    onUI_hide();
}

void timeTestActivity::onIntent(const Intent *intentPtr) {
    Activity::onIntent(intentPtr);
    onUI_intent(intentPtr);
}

bool timeTestActivity::onTimer(int id) {
    return onUI_Timer(id);
}

void timeTestActivity::rigesterActivityTimer() {
    int count = sizeof(REGISTER_ACTIVITY_TIMER_TAB) / sizeof(S_ACTIVITY_TIMEER);
    for (int i = 0; i < count; ++i) {
        registerUserTimer(REGISTER_ACTIVITY_TIMER_TAB[i].id,
                          REGISTER_ACTIVITY_TIMER_TAB[i].time);
    }
}

void timeTestActivity::registerUserTimer(int id, int time) {
    registerTimer(id, time);
}

void timeTestActivity::unregisterUserTimer(int id) {
    unregisterTimer(id);
}

void timeTestActivity::resetUserTimer(int id, int time) {
    resetTimer(id, time);
}
