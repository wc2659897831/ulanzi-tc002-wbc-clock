#include "uart/ProtocolSender.h"
#include "base/base.h"
#include "base/json_object.h"
#include <base/wifi.h>
#include "config/DeviceDefaults.h"
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
#include "pages/CodexStatusPage.h"
#include "services/OfficialCompatServer.h"
#include "services/MqttStatusService.h"
#include "ble/bluetooth_service.h"
#include "net/NetManager.h"
#include "utils/Painter.h"
#include "utils/Surface.h"
#include <atomic>
#include <chrono>
#include <ctime>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
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
static std::thread gStartupThread;
static std::atomic<int> gBootProgress(0);
static std::atomic<int> gBootState(0);
static bool gBootScreenVisible = true;
static uint64_t gBootSuccessAtMs = 0;
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

static uint64_t wallClockMs() {
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	return static_cast<uint64_t>(tv.tv_sec) * 1000ULL +
		static_cast<uint64_t>(tv.tv_usec) / 1000ULL;
}

static void setBootProgress(int progress) {
	if (progress < 0) {
		progress = 0;
	} else if (progress > 100) {
		progress = 100;
	}
	gBootProgress.store(progress);
}

static void drawBootScreen() {
	Surface surface(52, 16, Color(0, 0, 0));
	const bool success = gBootState.load() == 1;
	const std::string title = success ? "SUCCESS" : "LOADING";
	const Color titleColor = success ? Color(0, 255, 0) : Color(0, 220, 255);
	const int titleWidth = Painter::getInstance().getTextWidth(title, 0);
	Painter::getInstance().drawText(surface, (52 - titleWidth) / 2, 6,
		title, titleColor, 0);
	Painter::getInstance().drawRect(surface, 2, 10, 48, 4,
		Color(45, 45, 45), false);
	const int fillWidth = 46 * gBootProgress.load() / 100;
	if (fillWidth > 0) {
		Painter::getInstance().drawRect(surface, 3, 11, fillWidth, 2,
			titleColor, true);
	}
	std::vector<uint8_t> data;
	surface.extractRGB(data);
	PageBase::sendRawLedData(data);
}

static time_t utcEpochFromTm(const struct tm& value) {
	const int year = value.tm_year + 1900;
	const unsigned month = static_cast<unsigned>(value.tm_mon + 1);
	const int adjustedYear = year - (month <= 2 ? 1 : 0);
	const int era = (adjustedYear >= 0 ? adjustedYear : adjustedYear - 399) / 400;
	const unsigned yearOfEra = static_cast<unsigned>(adjustedYear - era * 400);
	const unsigned monthForDay = month > 2 ? month - 3 : month + 9;
	const unsigned dayOfYear =
		(153 * monthForDay + 2) / 5 +
		static_cast<unsigned>(value.tm_mday - 1);
	const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 -
		yearOfEra / 100 + dayOfYear;
	const int daysSinceEpoch = era * 146097 + static_cast<int>(dayOfEra) - 719468;
	return static_cast<time_t>(daysSinceEpoch) * 86400 +
		static_cast<time_t>(value.tm_hour) * 3600 +
		static_cast<time_t>(value.tm_min) * 60 + value.tm_sec;
}

static bool syncSystemTimeFromHttp() {
	static const char* hosts[] = {
		"www.baidu.com", "www.qq.com", "example.com"
	};
	for (const char* host : hosts) {
		addrinfo hints;
		std::memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		addrinfo* addresses = nullptr;
		if (getaddrinfo(host, "80", &hints, &addresses) != 0) {
			continue;
		}
		bool synchronized = false;
		for (addrinfo* address = addresses; address != nullptr && !synchronized;
				address = address->ai_next) {
			int fd = socket(address->ai_family, address->ai_socktype,
				address->ai_protocol);
			if (fd < 0) {
				continue;
			}
			struct timeval timeout = {3, 0};
			setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
			setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
			if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
				std::string request = "HEAD / HTTP/1.1\r\nHost: ";
				request += host;
				request += "\r\nConnection: close\r\n\r\n";
				if (send(fd, request.data(), request.size(), 0) ==
						static_cast<ssize_t>(request.size())) {
					char buffer[4096];
					std::string response;
					ssize_t received = 0;
					while (response.size() < sizeof(buffer) * 2 &&
							(received = recv(fd, buffer, sizeof(buffer), 0)) > 0) {
						response.append(buffer, static_cast<size_t>(received));
					}
					const std::string marker = "\r\nDate: ";
					size_t dateStart = response.find(marker);
					if (dateStart != std::string::npos) {
						dateStart += marker.size();
						size_t dateEnd = response.find("\r\n", dateStart);
						if (dateEnd != std::string::npos) {
							std::string date = response.substr(dateStart,
								dateEnd - dateStart);
							struct tm parsed;
							std::memset(&parsed, 0, sizeof(parsed));
							if (strptime(date.c_str(),
									"%a, %d %b %Y %H:%M:%S GMT", &parsed) != nullptr) {
								time_t epoch = utcEpochFromTm(parsed);
								if (epoch > 1600000000) {
									struct timeval current = {
										epoch, 0
									};
									synchronized = settimeofday(&current, nullptr) == 0;
								}
							}
						}
					}
				}
			}
			close(fd);
		}
		freeaddrinfo(addresses);
		if (synchronized) {
			LOGI_TRACE("mainLogic: system time synchronized from HTTP %s", host);
			return true;
		}
	}
	return false;
}

static bool syncSystemTime() {
	static const char* servers[] = {
		"ntp.aliyun.com", "pool.ntp.org", "time.nist.gov"
	};
	const uint32_t ntpEpochOffset = 2208988800UL;
	for (const char* server : servers) {
		addrinfo hints;
		std::memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_protocol = IPPROTO_UDP;
		addrinfo* addresses = nullptr;
		if (getaddrinfo(server, "123", &hints, &addresses) != 0) {
			continue;
		}
		bool synchronized = false;
		for (addrinfo* address = addresses; address != nullptr && !synchronized;
				address = address->ai_next) {
			int fd = socket(address->ai_family, address->ai_socktype,
				address->ai_protocol);
			if (fd < 0) {
				continue;
			}
			struct timeval timeout = {2, 0};
			setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
			unsigned char packet[48] = {0};
			packet[0] = 0x1b;
			if (sendto(fd, packet, sizeof(packet), 0, address->ai_addr,
					address->ai_addrlen) == static_cast<ssize_t>(sizeof(packet))) {
				ssize_t received = recvfrom(fd, packet, sizeof(packet), 0, nullptr,
					nullptr);
				if (received >= 48) {
					uint32_t seconds = (static_cast<uint32_t>(packet[40]) << 24) |
						(static_cast<uint32_t>(packet[41]) << 16) |
						(static_cast<uint32_t>(packet[42]) << 8) |
						static_cast<uint32_t>(packet[43]);
					uint32_t fraction = (static_cast<uint32_t>(packet[44]) << 24) |
						(static_cast<uint32_t>(packet[45]) << 16) |
						(static_cast<uint32_t>(packet[46]) << 8) |
						static_cast<uint32_t>(packet[47]);
					if (seconds > ntpEpochOffset) {
						struct timeval current = {
							static_cast<time_t>(seconds - ntpEpochOffset),
							static_cast<suseconds_t>((static_cast<uint64_t>(fraction) *
								1000000ULL) >> 32)
						};
						synchronized = settimeofday(&current, nullptr) == 0;
					}
				}
			}
			close(fd);
		}
		freeaddrinfo(addresses);
		if (synchronized) {
			LOGI_TRACE("mainLogic: system time synchronized from %s", server);
			return true;
		}
	}
	if (syncSystemTimeFromHttp()) {
		return true;
	}
	LOGW_TRACE("mainLogic: system time synchronization failed");
	return false;
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
		MenuIndicator::getInstance().show(8, 0);
	} else if (gTopPageIndex == MenuPage::TOP_MQTT) {
		MenuIndicator::getInstance().show(1, 0);
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
	} else if (gTopPageIndex == MenuPage::TOP_MQTT) {
		PageManager::getInstance().navigateTo("CodexStatusPage");
	} else if (gTopPageIndex == MenuPage::TOP_SETTINGS) {
		PageManager::getInstance().navigateTo("SettingsPage");
	} else {
		PageManager::getInstance().drawCurrentPage();
	}
}

static void enterStreamDisplayPage() {
	gMenuDepth = MENU_DEPTH_CHILD;
	gTopPageIndex = MenuPage::TOP_STREAM;
	gClockCountdownMs = 0;
	gLastClockCountdownValue = -1;

	MenuPage* menuPage = static_cast<MenuPage*>(
		PageManager::getInstance().getPage("MenuPage"));
	if (menuPage) {
		menuPage->setTopPage(MenuPage::TOP_STREAM);
	}

	StreamNoticeIndicator::getInstance().setTopMenuVisible(false);
	showChildIndicator();
	PageManager::getInstance().drawCurrentPage();
	StreamNoticeIndicator::getInstance().setStreamDisplayEnabled(true);
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
			gTopPageIndex == MenuPage::TOP_FUNCTIONS ||
			gTopPageIndex == MenuPage::TOP_MQTT) {
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
	setBootProgress(15);
	std::string ssid = device_defaults::WIFI_SSID;
	std::string pwd = device_defaults::WIFI_PASSWORD;
	std::string bleName;
	bool needConnect = device_defaults::WIFI_CONNECT;
	MqttStatusService::Config mqttConfig =
		MqttStatusService::defaultConfig();

	if (base::exists(CONFIG_PATH)) {
		try {
			base::JSONObject config = base::JSONObject::parseFromFile(CONFIG_PATH);
			ssid = config.getString("ssid", ssid);
			pwd = config.getString("pwd", pwd);
			needConnect = config.getBool("isConnect", needConnect);
			bleName = config.getString("ble", "");
			try {
				base::JSONObject mqtt = config.getObject("mqtt");
				mqttConfig.host = mqtt.getString("host", "");
				mqttConfig.port = mqtt.getInt("port", mqttConfig.port);
				mqttConfig.topic = mqtt.getString("topic", mqttConfig.topic);
				mqttConfig.clientId = mqtt.getString("clientId",
					mqttConfig.clientId);
				mqttConfig.username = mqtt.getString("username", "");
				mqttConfig.password = mqtt.getString("password", "");
				mqttConfig.enabled = mqtt.getBool("enabled",
					!mqttConfig.host.empty());
			} catch (...) {
				mqttConfig.host = "";
			}
			if (mqttConfig.host.empty()) {
				mqttConfig.host = config.getString("mqttHost", "");
				mqttConfig.port = config.getInt("mqttPort", mqttConfig.port);
				mqttConfig.topic = config.getString("mqttTopic",
					mqttConfig.topic);
				mqttConfig.clientId = config.getString("mqttClientId",
					mqttConfig.clientId);
				mqttConfig.username = config.getString("mqttUsername", "");
				mqttConfig.password = config.getString("mqttPassword", "");
				mqttConfig.enabled = config.getBool("mqttEnabled",
					!mqttConfig.host.empty());
			}
			LOGI_TRACE("mainLogic: config ssid=%s connect=%d ble=%s",
				ssid.c_str(), needConnect, bleName.c_str());
		} catch (...) {
			LOGE_TRACE("mainLogic: failed to parse %s", CONFIG_PATH);
		}
	} else {
		LOGW_TRACE("mainLogic: config not found: %s", CONFIG_PATH);
	}
	if (base::exists(device_defaults::WEB_CONFIG_PATH)) {
		try {
			base::JSONObject saved = base::JSONObject::parseFromFile(
				device_defaults::WEB_CONFIG_PATH);
			if (saved.has("wifi")) {
				base::JSONObject wifi = saved.getObject("wifi");
				ssid = wifi.getString("ssid", ssid);
				pwd = wifi.getString("password", pwd);
				needConnect = wifi.getBool("isConnect", needConnect);
			}
		} catch (...) {
			LOGW_TRACE("mainLogic: failed to load web settings");
		}
	}
	setBootProgress(40);
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
	setBootProgress(55);
	auto* page = static_cast<WifiTestPage*>(
		PageManager::getInstance().getPage("WifiTestPage"));
	if (page) {
		page->setWifiParams(ssid, pwd, needConnect);
	}
	if (needConnect && !ssid.empty() && WIFIMANAGER != nullptr &&
			!WIFIMANAGER->isConnected()) {
		LOGI_TRACE("mainLogic: connecting WiFi for background services");
		WIFIMANAGER->connect(ssid, pwd);
	}
	if (needConnect && !ssid.empty() && WIFIMANAGER != nullptr) {
		for (int attempt = 0; attempt < 30 && !WIFIMANAGER->isConnected();
				++attempt) {
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}
	}
	setBootProgress(70);
	syncSystemTime();
	setBootProgress(82);
	MqttStatusService::getInstance().configure(mqttConfig);
	MqttStatusService::getInstance().start();
	setBootProgress(100);
	gBootState.store(1);
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
	PageManager::getInstance().registerPage(std::unique_ptr<PageBase>(new CodexStatusPage()));
	PageManager::getInstance().registerPage(std::unique_ptr<PageBase>(new FunctionsPage()));
	PageManager::getInstance().registerPage(std::unique_ptr<PageBase>(new SettingsPage()));
	PageManager::getInstance().registerPage(std::unique_ptr<PageBase>(new MenuPage()));

	KeyManager::getInstance().start();
	KeyManager::getInstance().addKeyEventCallback(keyEventCb);
	StreamNoticeIndicator::getInstance().setNoticeCallback(streamNoticeCb);
    OfficialCompatServer::getInstance().start();
	setBootProgress(5);
	drawBootScreen();
	if (!gStartupThread.joinable()) {
		gStartupThread = std::thread(loadConfigAndStart);
	}
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
	if (gStartupThread.joinable()) {
		gStartupThread.join();
	}
	MqttStatusService::getInstance().stop();
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
	if (gBootScreenVisible) {
		if (gBootState.load() == 1) {
			if (gBootSuccessAtMs == 0) {
				gBootSuccessAtMs = wallClockMs();
				drawBootScreen();
			} else if (wallClockMs() - gBootSuccessAtMs >= 1500) {
				gBootScreenVisible = false;
				drawTopMenu();
			} else {
				drawBootScreen();
			}
		} else {
			drawBootScreen();
		}
		if (gBootScreenVisible) {
			return true;
		}
	}
	if (StreamNoticeIndicator::getInstance().consumeStreamDisplayRequest()) {
		enterStreamDisplayPage();
	}
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
			} else if (gMenuDepth == MENU_DEPTH_CHILD &&
					gTopPageIndex == MenuPage::TOP_MQTT) {
				auto* page = static_cast<CodexStatusPage*>(
					PageManager::getInstance().getPage("CodexStatusPage"));
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
