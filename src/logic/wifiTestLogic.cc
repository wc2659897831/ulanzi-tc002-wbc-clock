#pragma once
#include "uart/ProtocolSender.h"
#include "managers/KeyManager.h"
#include "managers/PageManager.h"

namespace {
static bool sShowingTimePage = false;

void keyEventCb(int keyCode, int keyStatus) {
	if(!PageManager::getInstance().onKeyEvent(keyCode, keyStatus)) {
		return;
	}
	switch(keyCode) {
	case E_KEYCODE_CLOCKWISE:
		if (sShowingTimePage) {
			EASYUICONTEXT->openActivity("audioTestActivity", nullptr);
		} else {
			sShowingTimePage = true;
			PageManager::getInstance().navigateTo("TimeTestPage");
		}
		break;
	case E_KEYCODE_ANTI_CLOCKWISE:
		if (sShowingTimePage) {
			sShowingTimePage = false;
			PageManager::getInstance().navigateTo("WifiTestPage");
		} else {
			EASYUICONTEXT->openActivity("rgbTestActivity", nullptr);
		}
		break;
	default:
		break;
	}
}

#define TIMER_REFRESH 1
#define TIMER_REFRESH_TIME 500
}

static S_ACTIVITY_TIMEER REGISTER_ACTIVITY_TIMER_TAB[] = {
	{TIMER_REFRESH, TIMER_REFRESH_TIME}
};

static void onUI_init(){

}

static void onUI_intent(const Intent *intentPtr) {
    if (intentPtr != NULL) {

    }
}

static void onUI_show() {
	sShowingTimePage = false;
	KeyManager::getInstance().addKeyEventCallback(keyEventCb);
	PageManager::getInstance().navigateTo("WifiTestPage");
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
	case TIMER_REFRESH:
		PageManager::getInstance().drawCurrentPage();
		break;
	default:
		break;
	}
    return true;
}

static bool onwifiTestActivityTouchEvent(const MotionEvent &ev) {
    switch (ev.mActionStatus) {
		case MotionEvent::E_ACTION_DOWN://触摸按下
			break;
		case MotionEvent::E_ACTION_MOVE://触摸滑动
			break;
		case MotionEvent::E_ACTION_UP:  //触摸抬起
			break;
		default:
			break;
	}
	return false;
}
