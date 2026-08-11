#pragma once
#include "uart/ProtocolSender.h"
#include "managers/KeyManager.h"
#include "managers/PageManager.h"

namespace {
void keyEventCb(int keyCode, int keyStatus) {
    if(!PageManager::getInstance().onKeyEvent(keyCode, keyStatus)) {
        return;
    }
    switch(keyCode) {
    case E_KEYCODE_CLOCKWISE:
        EASYUICONTEXT->openActivity("audioTestActivity", nullptr);
        break;
    case E_KEYCODE_ANTI_CLOCKWISE:
        EASYUICONTEXT->openActivity("wifiTestActivity", nullptr);
        break;
    default:
        break;
    }
}

#define TIMER_TIME_REFRESH 1
#define TIMER_TIME_REFRESH_TIME 1000
}

static S_ACTIVITY_TIMEER REGISTER_ACTIVITY_TIMER_TAB[] = {
    {TIMER_TIME_REFRESH, TIMER_TIME_REFRESH_TIME}
};

static void onUI_init(){
}

static void onUI_intent(const Intent *intentPtr) {
    if (intentPtr != NULL) {
    }
}

static void onUI_show() {
    KeyManager::getInstance().addKeyEventCallback(keyEventCb);
    PageManager::getInstance().navigateTo("TimeTestPage");
}

static void onUI_hide() {
    KeyManager::getInstance().removeKeyEventCallback(keyEventCb);
}

static void onUI_quit() {
    KeyManager::getInstance().removeKeyEventCallback(keyEventCb);
}

static void onProtocolDataUpdate(const SProtocolData &data) {
}

static bool onUI_Timer(int id){
    switch (id) {
    case TIMER_TIME_REFRESH:
        PageManager::getInstance().drawCurrentPage();
        break;
    default:
        break;
    }
    return true;
}
