#include "pages/FunctionsPage.h"

#include <cstdio>
#include <ctime>
#include <sys/time.h>

#include "base/base.h"
#include "managers/MenuIndicator.h"
#include "managers/McuManager.h"

namespace {

static const Color COLOR_BLACK(0, 0, 0);
static const Color COLOR_WHITE(255, 255, 255);
static const Color COLOR_DIM(55, 55, 55);
static const Color COLOR_RED(255, 60, 50);
static const Color COLOR_GREEN(60, 255, 120);
static const Color COLOR_YELLOW(255, 210, 60);
static const Color COLOR_CYAN(60, 220, 255);

#define DISPLAY_WIDTH 52
#define DISPLAY_HEIGHT 16
#define POMODORO_SECONDS (25 * 60)
#define POMODORO_REST_SECONDS (5 * 60)

static const char* DIGITS[] = {
	"111"
	"101"
	"101"
	"101"
	"111",
	"010"
	"110"
	"010"
	"010"
	"111",
	"111"
	"001"
	"111"
	"100"
	"111",
	"111"
	"001"
	"111"
	"001"
	"111",
	"101"
	"101"
	"111"
	"001"
	"001",
	"111"
	"100"
	"111"
	"001"
	"111",
	"111"
	"100"
	"111"
	"101"
	"111",
	"111"
	"001"
	"001"
	"001"
	"001",
	"111"
	"101"
	"111"
	"101"
	"111",
	"111"
	"101"
	"111"
	"001"
	"111"
};

static int clampInt(int value, int minValue, int maxValue) {
	if (value < minValue) {
		return minValue;
	}
	if (value > maxValue) {
		return maxValue;
	}
	return value;
}

} // namespace

FunctionsPage::FunctionsPage()
	: PageBase("FunctionsPage"),
	  mCurrentFunction(FUNC_POMODORO),
	  mPomodoroRunning(false),
	  mPomodoroResting(false),
	  mPomodoroRemainingSec(POMODORO_SECONDS),
	  mPomodoroLastTickMs(0),
	  mMiddlePressStartMs(0),
	  mMiddleLongResetDone(false),
	  mChargeFrame(0),
	  mTomatoFrame(0) {
}

FunctionsPage::~FunctionsPage() {
}

void FunctionsPage::onEnter() {
	LOGD_TRACE("FunctionsPage: onEnter");
	mPomodoroLastTickMs = nowMs();
	MenuIndicator::getInstance().show(FUNC_COUNT, mCurrentFunction);
	draw();
}

void FunctionsPage::onExit() {
	LOGD_TRACE("FunctionsPage: onExit");
	mMiddlePressStartMs = 0;
	mMiddleLongResetDone = false;
}

bool FunctionsPage::onKeyEvent(int keyCode, int keyStatus) {
	if ((keyCode == E_KEYCODE_CLOCKWISE || keyCode == E_KEYCODE_ANTI_CLOCKWISE) &&
			keyStatus == 0) {
		switchFunction(keyCode == E_KEYCODE_CLOCKWISE ? 1 : -1);
		return false;
	}

	if (keyCode == E_KEYCODE_MIDDLE_BUTTON &&
			mCurrentFunction == FUNC_POMODORO) {
		uint64_t currentMs = nowMs();
		if (keyStatus == 1) {
			mMiddlePressStartMs = currentMs;
			mMiddleLongResetDone = false;
			return false;
		}
		if (keyStatus == 0 && mMiddlePressStartMs > 0) {
			if (!mMiddleLongResetDone &&
					currentMs - mMiddlePressStartMs >= 3000ULL) {
				resetPomodoro();
				mMiddleLongResetDone = true;
			}
			if (!mMiddleLongResetDone) {
				togglePomodoro();
			}
			mMiddlePressStartMs = 0;
			mMiddleLongResetDone = false;
			return false;
		}
	}

	if (keyStatus != 1) {
		return true;
	}

	switch (keyCode) {
	case E_KEYCODE_LEFT_BUTTON:
		switchFunction(-1);
		return false;
	case E_KEYCODE_RIGHT_BUTTON:
		switchFunction(1);
		return false;
	default:
		return true;
	}
}

void FunctionsPage::draw() {
	uint64_t currentMs = nowMs();
	updatePomodoro(currentMs);
	updateMiddleLongPress(currentMs);
	Surface surface(DISPLAY_WIDTH, DISPLAY_HEIGHT, COLOR_BLACK);

	if (mCurrentFunction == FUNC_BATTERY) {
		drawBattery(surface);
	} else {
		drawPomodoro(surface);
	}

	std::vector<uint8_t> data;
	surface.extractRGB(data);
	MenuIndicator::getInstance().show(FUNC_COUNT, mCurrentFunction);
	sendLedData(data);
}

uint64_t FunctionsPage::nowMs() const {
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	return static_cast<uint64_t>(tv.tv_sec) * 1000ULL +
		static_cast<uint64_t>(tv.tv_usec) / 1000ULL;
}

void FunctionsPage::switchFunction(int delta) {
	mCurrentFunction += delta;
	while (mCurrentFunction < 0) {
		mCurrentFunction += FUNC_COUNT;
	}
	while (mCurrentFunction >= FUNC_COUNT) {
		mCurrentFunction -= FUNC_COUNT;
	}
	MenuIndicator::getInstance().show(FUNC_COUNT, mCurrentFunction);
	draw();
}

void FunctionsPage::togglePomodoro() {
	mPomodoroRunning = !mPomodoroRunning;
	mPomodoroLastTickMs = nowMs();
	draw();
}

void FunctionsPage::resetPomodoro() {
	mPomodoroRunning = false;
	mPomodoroResting = false;
	mPomodoroRemainingSec = POMODORO_SECONDS;
	mPomodoroLastTickMs = nowMs();
	draw();
}

void FunctionsPage::advancePomodoroPhase() {
	mPomodoroResting = !mPomodoroResting;
	mPomodoroRemainingSec = mPomodoroResting ?
		POMODORO_REST_SECONDS : POMODORO_SECONDS;
}

void FunctionsPage::updatePomodoro(uint64_t currentMs) {
	if (!mPomodoroRunning) {
		mPomodoroLastTickMs = currentMs;
		return;
	}

	if (currentMs <= mPomodoroLastTickMs) {
		return;
	}

	int elapsedSec = static_cast<int>((currentMs - mPomodoroLastTickMs) / 1000);
	if (elapsedSec <= 0) {
		return;
	}

	mPomodoroRemainingSec -= elapsedSec;
	mPomodoroLastTickMs += static_cast<uint64_t>(elapsedSec) * 1000ULL;
	if (mPomodoroRemainingSec <= 0) {
		advancePomodoroPhase();
	}
}

void FunctionsPage::updateMiddleLongPress(uint64_t currentMs) {
	if (mCurrentFunction != FUNC_POMODORO ||
			mMiddlePressStartMs <= 0 ||
			mMiddleLongResetDone) {
		return;
	}
	if (currentMs - mMiddlePressStartMs >= 3000ULL) {
		mPomodoroRunning = false;
		mPomodoroResting = false;
		mPomodoroRemainingSec = POMODORO_SECONDS;
		mPomodoroLastTickMs = currentMs;
		mMiddleLongResetDone = true;
	}
}

void FunctionsPage::drawPomodoro(Surface& surface) {
	drawTomatoIcon(surface, 1, 5);
	const int minutes = mPomodoroRemainingSec / 60;
	const int seconds = mPomodoroRemainingSec % 60;
	const Color textColor = mPomodoroResting ? COLOR_YELLOW : COLOR_WHITE;
	const Color colonColor = mPomodoroRunning ? COLOR_WHITE : COLOR_YELLOW;
	drawTwoDigitNumber(surface, 14, 13, minutes, textColor);
	Painter::getInstance().drawRect(surface, 30, 5, 2, 2, colonColor, true);
	Painter::getInstance().drawRect(surface, 30, 10, 2, 2, colonColor, true);
	drawTwoDigitNumber(surface, 34, 13, seconds, textColor);
	if (mPomodoroRunning) {
		mTomatoFrame++;
	}
}

void FunctionsPage::drawTomatoIcon(Surface& surface, int x, int y) {
	static const int8_t shine[][2] = {
		{3, 2}, {6, 3}, {7, 6}, {4, 8}, {1, 6}, {1, 3}
	};
	const int frame = mTomatoFrame % 6;

	Painter::getInstance().drawRect(surface, x + 2, y + 2, 8, 8, COLOR_RED, true);
	Painter::getInstance().drawPixel(surface, x + 1, y + 4, COLOR_RED);
	Painter::getInstance().drawPixel(surface, x + 1, y + 7, COLOR_RED);
	Painter::getInstance().drawPixel(surface, x + 10, y + 4, COLOR_RED);
	Painter::getInstance().drawPixel(surface, x + 10, y + 7, COLOR_RED);
	Painter::getInstance().drawPixel(surface, x + 3, y + 1, COLOR_GREEN);
	Painter::getInstance().drawPixel(surface, x + 4, y, COLOR_GREEN);
	Painter::getInstance().drawPixel(surface, x + 5, y + 1, COLOR_GREEN);
	Painter::getInstance().drawPixel(surface, x + 6, y, COLOR_GREEN);
	Painter::getInstance().drawPixel(surface, x + 7, y + 1, COLOR_GREEN);
	Painter::getInstance().drawPixel(
		surface,
		x + shine[frame][0],
		y + shine[frame][1],
		COLOR_WHITE);
}

void FunctionsPage::drawBattery(Surface& surface) {
	drawHeader(surface, "BATT");
	std::pair<int, int> battery = McuManager::getInstance().queryBatteryPower();
	int percent = clampInt(battery.first, 0, 100);
	bool charging = battery.second != 0;
	drawBatteryIcon(surface, 8, 6, percent, charging);

	char text[5] = {0};
	snprintf(text, sizeof(text), "%d", percent);
	Painter::getInstance().drawText(surface, 36, 13, text, COLOR_WHITE, 0);
	if (charging) {
		int x = 35 + (mChargeFrame % 4);
		Painter::getInstance().drawPixel(surface, x, 5, COLOR_YELLOW);
		Painter::getInstance().drawPixel(surface, x + 1, 6, COLOR_YELLOW);
		Painter::getInstance().drawPixel(surface, x, 7, COLOR_YELLOW);
		mChargeFrame++;
	}
}

void FunctionsPage::drawBatteryIcon(Surface& surface, int x, int y, int percent, bool charging) {
	(void) charging;
	Painter::getInstance().drawRect(surface, x, y, 24, 8, COLOR_WHITE, false);
	Painter::getInstance().drawRect(surface, x + 24, y + 2, 2, 4, COLOR_WHITE, true);
	int fillW = percent * 22 / 100;
	if (fillW > 0) {
		Color color = percent < 20 ? COLOR_RED : (percent < 50 ? COLOR_YELLOW : COLOR_GREEN);
		Painter::getInstance().drawRect(surface, x + 1, y + 1, fillW, 6, color, true);
	}
}

void FunctionsPage::drawHeader(Surface& surface, const char* text) {
	Painter::getInstance().drawText(surface, 0, 4, text, COLOR_DIM, 0);
}

void FunctionsPage::drawTwoDigitNumber(Surface& surface, int x, int y, int value, const Color& color) {
	value = clampInt(value, 0, 99);
	drawDigit(surface, x, y - 10, value / 10, color);
	drawDigit(surface, x + 8, y - 10, value % 10, color);
}

void FunctionsPage::drawDigit(Surface& surface, int x, int y, int digit, const Color& color) {
	if (digit < 0 || digit > 9) {
		return;
	}
	const char* bitmap = DIGITS[digit];
	for (int row = 0; row < 5; ++row) {
		for (int col = 0; col < 3; ++col) {
			if (bitmap[row * 3 + col] != '1') {
				continue;
			}
			Painter::getInstance().drawRect(
				surface, x + col * 2, y + row * 2, 2, 2, color, true);
		}
	}
}
