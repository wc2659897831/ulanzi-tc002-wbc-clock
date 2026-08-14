#include "uart/ProtocolSender.h"
#include "base/base.h"
#include "base/json_object.h"
#include <base/wifi.h>
#include "utils/SpiHelper.h"
#include "utils/GpioHelper.h"
#include "managers/KeyManager.h"
#include "managers/MenuIndicator.h"
#include "managers/PageManager.h"
#include "managers/McuManager.h"
#include "managers/StreamNoticeIndicator.h"
#include "pages/MenuPage.h"
#include "pages/RgbTestPage.h"
#include "pages/BtnTestPage.h"
#include "pages/AudioTestPage.h"
#include "pages/FunctionsPage.h"
#include "pages/SettingsPage.h"
#include "pages/WifiTestPage.h"
#include "pages/TimeTestPage.h"
#include "services/OfficialCompatServer.h"
#include "ble/bluetooth_service.h"
#include "net/NetManager.h"
#include <thread>
#include <os/SystemProperties.h>
#define WIFIMANAGER NETMANAGER->getWifiManager()
#define CONFIG_PATH "/mnt/usb1/test.cfg"
namespace {

#define TIMER_MENU_CLOCK_REFRESH 1
#define TIMER_MENU_CLOCK_REFRESH_TIME 500
#define TIMER_FAST_REFRESH 2
#define TIMER_FAST_REFRESH_TIME 100

enum MenuDepth {
	MENU_DEPTH_TOP = 0,
	MENU_DEPTH_CHILD
};

static int gTopPageIndex = MenuPage::TOP_CLOCK;
static int gTestPageIndex = 0;
static MenuDepth gMenuDepth = MENU_DEPTH_TOP;
static int gClockCountdownMs = 3000;
static int gLastClockCountdownValue = -1;
static bool gStreamNoticeNeedsRefresh = false;

static const char* TEST_PAGE_NAMES[] = {
	"BtnTestPage",
	"RgbTestPage",
	"AudioTestPage",
	"WifiTestPage"
};
static const int TEST_PAGE_COUNT =
	sizeof(TEST_PAGE_NAMES) / sizeof(TEST_PAGE_NAMES[0]);

static int wrapIndex(int value, int count) {
	if (count <= 0) {
		return 0;
	}
	while (value < 0) {
		value += count;
	}
	while (value >= count) {
		value -= count;
	}
	return value;
}

static void drawTopMenu() {
	MenuIndicator::getInstance().show(MenuPage::TOP_COUNT, gTopPageIndex);
	StreamNoticeIndicator::getInstance().setTopMenuVisible(true);
	StreamNoticeIndicator::getInstance().setStreamDisplayEnabled(false);
	MenuPage* menuPage = static_cast<MenuPage*>(
		PageManager::getInstance().getPage("MenuPage"));
	if (menuPage) {
		menuPage->setTopPage(gTopPageIndex);
		menuPage->setClockCountdown(
			gTopPageIndex == MenuPage::TOP_CLOCK ?
			((gClockCountdownMs + 999) / 1000) : 0);
	}
	PageManager::getInstance().navigateTo("MenuPage");
}

static void showChildIndicator() {
	if (gTopPageIndex == MenuPage::TOP_TEST) {
		MenuIndicator::getInstance().show(TEST_PAGE_COUNT, gTestPageIndex);
	} else if (gTopPageIndex == MenuPage::TOP_CLOCK) {
		MenuIndicator::getInstance().show(2, 0);
	} else if (gTopPageIndex == MenuPage::TOP_FUNCTIONS) {
		MenuIndicator::getInstance().show(4, 0);
	} else {
		MenuIndicator::getInstance().hide();
	}
}

static void enterChildPage() {
	gMenuDepth = MENU_DEPTH_CHILD;
	StreamNoticeIndicator::getInstance().setTopMenuVisible(false);
	StreamNoticeIndicator::getInstance().setStreamDisplayEnabled(
		gTopPageIndex == MenuPage::TOP_STREAM);
	showChildIndicator();
	if (gTopPageIndex == MenuPage::TOP_TEST) {
		PageManager::getInstance().navigateTo(TEST_PAGE_NAMES[gTestPageIndex]);
	} else if (gTopPageIndex == MenuPage::TOP_CLOCK) {
		PageManager::getInstance().navigateTo("TimeTestPage");
	} else if (gTopPageIndex == MenuPage::TOP_FUNCTIONS) {
		PageManager::getInstance().navigateTo("FunctionsPage");
	} else if (gTopPageIndex == MenuPage::TOP_SETTINGS) {
		PageManager::getInstance().navigateTo("SettingsPage");
	} else {
		PageManager::getInstance().drawCurrentPage();
	}
}

static void exitChildPage() {
	if (gTopPageIndex == MenuPage::TOP_STREAM) {
		OfficialCompatServer::getInstance().stopDisplay();
	}
	StreamNoticeIndicator::getInstance().setStreamDisplayEnabled(false);
	gClockCountdownMs = 3000;
	gLastClockCountdownValue = -1;
	gMenuDepth = MENU_DEPTH_TOP;
	drawTopMenu();
}

static void streamNoticeCb() {
	if (gMenuDepth == MENU_DEPTH_TOP) {
		gStreamNoticeNeedsRefresh = true;
		PageManager::getInstance().drawCurrentPage();
	}
}

static void handleTopScroll(int delta) {
	gTopPageIndex = wrapIndex(gTopPageIndex + delta, MenuPage::TOP_COUNT);
	gClockCountdownMs = 3000;
	gLastClockCountdownValue = -1;
	drawTopMenu();
}

static void updateClockTopCountdown() {
	if (gMenuDepth != MENU_DEPTH_TOP || gTopPageIndex != MenuPage::TOP_CLOCK) {
		return;
	}

	gClockCountdownMs -= TIMER_MENU_CLOCK_REFRESH_TIME;
	if (gClockCountdownMs <= 0) {
		enterChildPage();
		return;
	}

	const int countdown = (gClockCountdownMs + 999) / 1000;
	if (countdown != gLastClockCountdownValue) {
		gLastClockCountdownValue = countdown;
		drawTopMenu();
	}
}

static void handleChildScroll(int delta) {
	if (gTopPageIndex == MenuPage::TOP_SETTINGS ||
			gTopPageIndex == MenuPage::TOP_CLOCK ||
			gTopPageIndex == MenuPage::TOP_FUNCTIONS) {
		PageManager::getInstance().onKeyEvent(
			delta > 0 ? E_KEYCODE_CLOCKWISE : E_KEYCODE_ANTI_CLOCKWISE, 0);
		return;
	}

	if (gTopPageIndex != MenuPage::TOP_TEST) {
		return;
	}
	gTestPageIndex = wrapIndex(gTestPageIndex + delta, TEST_PAGE_COUNT);
	showChildIndicator();
	PageManager::getInstance().navigateTo(TEST_PAGE_NAMES[gTestPageIndex]);
}

void keyEventCb(int keyCode, int keyStatus) {
	if (keyCode == E_KEYCODE_CLOCKWISE && keyStatus == 0) {
		if (gMenuDepth == MENU_DEPTH_TOP) {
			handleTopScroll(1);
		} else {
			handleChildScroll(1);
		}
		return;
	}

	if (keyCode == E_KEYCODE_ANTI_CLOCKWISE && keyStatus == 0) {
		if (gMenuDepth == MENU_DEPTH_TOP) {
			handleTopScroll(-1);
		} else {
			handleChildScroll(-1);
		}
		return;
	}

	if (keyCode == E_KEYCODE_KNOB_BUTTON && keyStatus == 1) {
		if (gMenuDepth == MENU_DEPTH_TOP) {
			enterChildPage();
		} else {
			exitChildPage();
		}
		return;
	}

	if (gMenuDepth == MENU_DEPTH_CHILD) {
		PageManager::getInstance().onKeyEvent(keyCode, keyStatus);
	}
}

void loadConfigAndStart() {
	if (!base::wifiOnAndWait(10)) {
		LOGE_TRACE("mainLogic: WiFi failed to start");
	}
	std::string ssid, pwd, bleName;
	bool needConnect = false;

	if (base::exists(CONFIG_PATH)) {
		try {
			base::JSONObject config = base::JSONObject::parseFromFile(CONFIG_PATH);
			ssid = config.getString("ssid", "");
			pwd = config.getString("pwd", "");
			needConnect = config.getBool("isConnect", false);
			bleName = config.getString("ble", "");
			LOGI_TRACE("mainLogic: config ssid=%s connect=%d ble=%s",
				ssid.c_str(), needConnect, bleName.c_str());
		} catch (...) {
			LOGE_TRACE("mainLogic: failed to parse %s", CONFIG_PATH);
		}
	} else {
		LOGW_TRACE("mainLogic: config not found: %s", CONFIG_PATH);
	}
	if (bleName.empty()) {
		std::string mac = base::wifiMacAddress();
		if (mac.size() >= 4) {
			bleName = "Ulanzi TC002 " + mac.substr(mac.size() - 4);
		} else {
			bleName = "Ulanzi TC002";
		}
	}
	BluetoothParams bleParams;
	bleParams.name = bleName;
	BluetoothService::instance().start(bleParams);
	auto* page = static_cast<WifiTestPage*>(
		PageManager::getInstance().getPage("WifiTestPage"));
	if (page) {
		page->setWifiParams(ssid, pwd, needConnect);
	}
}

}

static S_ACTIVITY_TIMEER REGISTER_ACTIVITY_TIMER_TAB[] = {
	{TIMER_MENU_CLOCK_REFRESH, TIMER_MENU_CLOCK_REFRESH_TIME},
	{TIMER_FAST_REFRESH, TIMER_FAST_REFRESH_TIME}
};

/**
 * 当界面构造时触发
 */
static void onUI_init(){
	//防砖属性
	SystemProperties::setString("sys.zkapp.state", "running");
	//初始化串口
	McuManager::getInstance().initialize(new PixelMcuProto::McuParse("/dev/ttyS1", 1500000));

	//每次开机必须先请求一下版本,才可以显示画面
	std::string mcuVer;
	McuManager::getInstance().queryMcuVersion(mcuVer);
	LOGI_TRACE("mcuVer : [%s]", mcuVer.c_str());

	PageManager::getInstance().registerPage(std::unique_ptr<PageBase>(new BtnTestPage()));
	PageManager::getInstance().registerPage(std::unique_ptr<PageBase>(new RgbTestPage()));
	PageManager::getInstance().registerPage(std::unique_ptr<PageBase>(new AudioTestPage()));
	PageManager::getInstance().registerPage(std::unique_ptr<PageBase>(new WifiTestPage()));
	PageManager::getInstance().registerPage(std::unique_ptr<PageBase>(new TimeTestPage()));
	PageManager::getInstance().registerPage(std::unique_ptr<PageBase>(new FunctionsPage()));
	PageManager::getInstance().registerPage(std::unique_ptr<PageBase>(new SettingsPage()));
	PageManager::getInstance().registerPage(std::unique_ptr<PageBase>(new MenuPage()));

	KeyManager::getInstance().start();
	KeyManager::getInstance().addKeyEventCallback(keyEventCb);
	StreamNoticeIndicator::getInstance().setNoticeCallback(streamNoticeCb);
    loadConfigAndStart();
    OfficialCompatServer::getInstance().start();
    drawTopMenu();
}

/**
 * 当切换到该界面时触发
 */
static void onUI_intent(const Intent *intentPtr) {
    if (intentPtr != NULL) {
        //TODO
    }
}

/*
 * 当界面显示时触发
 */
static void onUI_show() {
}

/*
 * 当界面隐藏时触发
 */
static void onUI_hide() {

}

/*
 * 当界面完全退出时触发
 */
static void onUI_quit() {
	KeyManager::getInstance().removeKeyEventCallback(keyEventCb);
	OfficialCompatServer::getInstance().stop();
}

/**
 * 串口数据回调接口
 */
static void onProtocolDataUpdate(const SProtocolData &data) {

}

/**
 * 定时器触发函数
 * 不建议在此函数中写耗时操作，否则将影响UI刷新
 * 参数： id
 *         当前所触发定时器的id，与注册时的id相同
 * 返回值: true
 *             继续运行当前定时器
 *         false
 *             停止运行当前定时器
 */
static bool onUI_Timer(int id){
	switch (id) {
		case TIMER_MENU_CLOCK_REFRESH:
			if (gMenuDepth == MENU_DEPTH_CHILD &&
					gTopPageIndex == MenuPage::TOP_CLOCK) {
				PageManager::getInstance().drawCurrentPage();
			}
			updateClockTopCountdown();
			break;
		case TIMER_FAST_REFRESH:
			if (gMenuDepth == MENU_DEPTH_CHILD &&
					gTopPageIndex == MenuPage::TOP_FUNCTIONS) {
				auto* page = static_cast<FunctionsPage*>(
					PageManager::getInstance().getPage("FunctionsPage"));
				if (page && page->needsFastRefresh()) {
					PageManager::getInstance().drawCurrentPage();
				}
			} else if (gMenuDepth == MENU_DEPTH_TOP &&
					(gStreamNoticeNeedsRefresh ||
					 StreamNoticeIndicator::getInstance().isNoticeActive())) {
				PageManager::getInstance().drawCurrentPage();
				gStreamNoticeNeedsRefresh =
					StreamNoticeIndicator::getInstance().isNoticeActive();
			}
			break;
		default:
			break;
	}
    return true;
}

/**
 * 有新的触摸事件时触发
 * 参数：ev
 *         新的触摸事件
 * 返回值：true
 *            表示该触摸事件在此被拦截，系统不再将此触摸事件传递到控件上
 *         false
 *            触摸事件将继续传递到控件上
 */
static bool onmainActivityTouchEvent(const MotionEvent &ev) {
    switch (ev.mActionStatus) {
		case MotionEvent::E_ACTION_DOWN://触摸按下
			//LOGD("时刻 = %ld 坐标  x = %d, y = %d", ev.mEventTime, ev.mX, ev.mY);
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
