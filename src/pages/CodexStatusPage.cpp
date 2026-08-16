#include "pages/CodexStatusPage.h"

#include <sys/time.h>

#include "base/base.h"
#include "managers/MenuIndicator.h"

namespace {

static const Color COLOR_BLACK(0, 0, 0);
static const Color COLOR_WHITE(255, 255, 255);
static const Color COLOR_DIM(28, 28, 28);
static const Color COLOR_RED(255, 60, 50);
static const Color COLOR_RED_DIM(45, 4, 8);
static const Color COLOR_YELLOW(255, 210, 60);
static const Color COLOR_YELLOW_DIM(48, 36, 5);
static const Color COLOR_GREEN(60, 255, 120);
static const Color COLOR_GREEN_DIM(7, 42, 18);
static const Color COLOR_CYAN(0, 220, 255);
static const Color COLOR_BLUE(50, 120, 255);

#define DISPLAY_WIDTH 52
#define DISPLAY_HEIGHT 16

} // namespace

CodexStatusPage::CodexStatusPage()
	: PageBase("CodexStatusPage"),
	  mFrame(0) {
}

CodexStatusPage::~CodexStatusPage() {
}

void CodexStatusPage::onEnter() {
	LOGD_TRACE("CodexStatusPage: onEnter");
	MenuIndicator::getInstance().show(1, 0);
	draw();
}

void CodexStatusPage::onExit() {
	LOGD_TRACE("CodexStatusPage: onExit");
}

bool CodexStatusPage::onKeyEvent(int keyCode, int keyStatus) {
	(void) keyCode;
	(void) keyStatus;
	return true;
}

bool CodexStatusPage::needsFastRefresh() const {
	return true;
}

void CodexStatusPage::draw() {
	const uint64_t currentMs = nowMs();
	const MqttStatusService::Snapshot snapshot =
		MqttStatusService::getInstance().snapshot();
	Surface surface(DISPLAY_WIDTH, DISPLAY_HEIGHT, COLOR_BLACK);

	if (!snapshot.configured) {
		drawNoConfig(surface);
	} else if (!snapshot.connected) {
		drawDisconnected(surface, snapshot);
	} else {
		drawStatusLights(surface, snapshot, currentMs);
		drawStatusText(surface, snapshot);
	}

	std::vector<uint8_t> data;
	surface.extractRGB(data);
	sendLedData(data);
	mFrame++;
}

uint64_t CodexStatusPage::nowMs() const {
	timeval tv;
	gettimeofday(&tv, nullptr);
	return static_cast<uint64_t>(tv.tv_sec) * 1000ULL +
		static_cast<uint64_t>(tv.tv_usec) / 1000ULL;
}

void CodexStatusPage::drawStatusLights(Surface& surface,
		const MqttStatusService::Snapshot& snapshot, uint64_t currentMs) {
	bool red = false;
	bool yellow = false;
	bool green = false;

	switch (snapshot.status) {
	case MqttStatusService::STATUS_IDLE:
		green = true;
		break;
	case MqttStatusService::STATUS_WORKING: {
		const int phase = static_cast<int>((currentMs / 280) % 3);
		green = phase == 0;
		yellow = phase == 1;
		red = phase == 2;
		break;
	}
	case MqttStatusService::STATUS_ATTENTION:
		yellow = ((currentMs / 350) % 2) == 0;
		break;
	case MqttStatusService::STATUS_BLOCKED:
		red = ((currentMs / 350) % 2) == 0;
		break;
	case MqttStatusService::STATUS_OFF:
	case MqttStatusService::STATUS_UNKNOWN:
	default:
		break;
	}

	drawLight(surface, 7, 8, COLOR_RED, red);
	drawLight(surface, 18, 8, COLOR_YELLOW, yellow);
	drawLight(surface, 29, 8, COLOR_GREEN, green);
}

void CodexStatusPage::drawLight(Surface& surface, int cx, int cy,
		const Color& color, bool active) {
	Color dim = COLOR_DIM;
	if (color.r > color.g && color.r > color.b) {
		dim = COLOR_RED_DIM;
	} else if (color.g > color.r) {
		dim = COLOR_GREEN_DIM;
	} else {
		dim = COLOR_YELLOW_DIM;
	}

	Painter::getInstance().drawCircle(surface, cx, cy, 4,
		active ? color : dim, true);
	Painter::getInstance().drawCircle(surface, cx, cy, 5,
		active ? COLOR_WHITE : COLOR_DIM, false);
	if (active) {
		Painter::getInstance().drawPixel(surface, cx - 1, cy - 2,
			COLOR_WHITE);
	}
}

void CodexStatusPage::drawStatusText(Surface& surface,
		const MqttStatusService::Snapshot& snapshot) {
	const char* top = "CODEX";
	const char* bottom = "OFF";
	Color color = COLOR_DIM;

	switch (snapshot.status) {
	case MqttStatusService::STATUS_IDLE:
		bottom = "IDLE";
		color = COLOR_GREEN;
		break;
	case MqttStatusService::STATUS_WORKING:
		bottom = "WORK";
		color = COLOR_CYAN;
		break;
	case MqttStatusService::STATUS_ATTENTION:
		bottom = "ASK";
		color = COLOR_YELLOW;
		break;
	case MqttStatusService::STATUS_BLOCKED:
		bottom = "BLOCK";
		color = COLOR_RED;
		break;
	case MqttStatusService::STATUS_UNKNOWN:
		bottom = "UNK";
		color = COLOR_BLUE;
		break;
	case MqttStatusService::STATUS_OFF:
	default:
		bottom = "OFF";
		color = COLOR_DIM;
		break;
	}

	Painter::getInstance().drawText(surface, 35, 6, top, COLOR_WHITE, 0);
	Painter::getInstance().drawText(surface, 35, 14, bottom, color, 0);

	if (snapshot.lastSeenSeconds > 0 && snapshot.lastSeenSeconds < 100) {
		const int dots = 1 + static_cast<int>(snapshot.lastSeenSeconds / 10);
		for (int i = 0; i < dots && i < 6; ++i) {
			Painter::getInstance().drawPixel(surface, 35 + i * 2, 0,
				COLOR_CYAN);
		}
	}
}

void CodexStatusPage::drawNoConfig(Surface& surface) {
	Painter::getInstance().drawText(surface, 0, 6, "MQTT", COLOR_YELLOW, 0);
	Painter::getInstance().drawText(surface, 0, 14, "CONF", COLOR_RED, 0);
	Painter::getInstance().drawRect(surface, 31, 3, 17, 10, COLOR_DIM, false);
	Painter::getInstance().drawLine(surface, 34, 10, 44, 10, COLOR_RED);
	Painter::getInstance().drawLine(surface, 44, 10, 41, 7, COLOR_RED);
	Painter::getInstance().drawLine(surface, 44, 10, 41, 13, COLOR_RED);
}

void CodexStatusPage::drawDisconnected(Surface& surface,
		const MqttStatusService::Snapshot& snapshot) {
	(void) snapshot;
	const bool blink = (mFrame / 4) % 2 == 0;
	Painter::getInstance().drawText(surface, 0, 6, "MQTT", COLOR_WHITE, 0);
	Painter::getInstance().drawText(surface, 0, 14, "OFF",
		blink ? COLOR_RED : COLOR_DIM, 0);
	Painter::getInstance().drawCircle(surface, 35, 8, 4,
		blink ? COLOR_RED : COLOR_RED_DIM, true);
	Painter::getInstance().drawCircle(surface, 46, 8, 4, COLOR_DIM, false);
	Painter::getInstance().drawLine(surface, 39, 8, 42, 8, COLOR_DIM);
}
