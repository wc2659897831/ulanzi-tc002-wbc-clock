#include "pages/FunctionsPage.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/time.h>

#include "base/base.h"
#include "managers/MenuIndicator.h"
#include "managers/McuManager.h"
#include "net/NetManager.h"

#define WIFIMANAGER NETMANAGER->getWifiManager()

namespace {

static const Color COLOR_BLACK(0, 0, 0);
static const Color COLOR_WHITE(255, 255, 255);
static const Color COLOR_DIM(55, 55, 55);
static const Color COLOR_RED(255, 60, 50);
static const Color COLOR_RED_DARK(145, 12, 24);
static const Color COLOR_RED_DEEP(90, 0, 18);
static const Color COLOR_GREEN(60, 255, 120);
static const Color COLOR_GREEN_DARK(20, 130, 55);
static const Color COLOR_YELLOW(255, 210, 60);
static const Color COLOR_CYAN(60, 220, 255);
static const Color COLOR_MAGENTA(255, 80, 210);

#define DISPLAY_WIDTH 52
#define DISPLAY_HEIGHT 16
#define POMODORO_SECONDS (25 * 60)
#define POMODORO_REST_SECONDS (5 * 60)
#define IP_SCROLL_SPEED_MS 70
#define IP_QUERY_INTERVAL_MS 2000
#define LARGE_DIGIT_SCALE 2
#define LARGE_DIGIT_WIDTH (3 * LARGE_DIGIT_SCALE)
#define LARGE_DIGIT_HEIGHT (5 * LARGE_DIGIT_SCALE)
#define LARGE_CHAR_GAP 2
#define DOT_WIDTH 1
#define MIC_BAR_COUNT DISPLAY_WIDTH
#define MIC_IDLE_PERCENT 3
#define MIC_PALETTE_COUNT 5

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

static Color blendColor(const Color& from, const Color& to, int value,
		int maxValue) {
	if (maxValue <= 0) {
		return to;
	}
	value = clampInt(value, 0, maxValue);
	return Color(
		static_cast<Color::byte>(from.r + (to.r - from.r) * value / maxValue),
		static_cast<Color::byte>(from.g + (to.g - from.g) * value / maxValue),
		static_cast<Color::byte>(from.b + (to.b - from.b) * value / maxValue));
}

static Color scaleColor(const Color& color, int percent) {
	percent = clampInt(percent, 0, 100);
	return Color(
		static_cast<Color::byte>(color.r * percent / 100),
		static_cast<Color::byte>(color.g * percent / 100),
		static_cast<Color::byte>(color.b * percent / 100));
}

struct MicPalette {
	Color bottom;
	Color lower;
	Color upper;
	Color top;
};

static const MicPalette& micPalette(int paletteIndex) {
	static const MicPalette PALETTES[MIC_PALETTE_COUNT] = {
		{ Color(60, 255, 120), Color(60, 220, 255), Color(255, 210, 60), Color(255, 80, 210) },
		{ Color(0, 190, 255), Color(80, 255, 150), Color(255, 230, 40), Color(255, 70, 80) },
		{ Color(80, 255, 80), Color(80, 180, 255), Color(190, 90, 255), Color(255, 80, 150) },
		{ Color(255, 95, 70), Color(255, 220, 50), Color(80, 255, 170), Color(40, 190, 255) },
		{ Color(80, 210, 255), Color(120, 255, 100), Color(255, 160, 50), Color(255, 60, 200) }
	};
	return PALETTES[clampInt(paletteIndex, 0, MIC_PALETTE_COUNT - 1)];
}

static Color micGradientColor(int rowFromBottom, int paletteIndex) {
	const MicPalette& palette = micPalette(paletteIndex);
	const int pos = clampInt(rowFromBottom * 100 / (DISPLAY_HEIGHT - 1), 0, 100);
	if (pos < 38) {
		return blendColor(palette.bottom, palette.lower, pos, 38);
	}
	if (pos < 72) {
		return blendColor(palette.lower, palette.upper, pos - 38, 34);
	}
	return blendColor(palette.upper, palette.top, pos - 72, 28);
}

static int micColumnTarget(int column, int frame, int activeRows) {
	if (activeRows <= 0) {
		return 1;
	}

	const int waveA = (frame * 3 + column * 5) % 17;
	const int waveB = (frame + column * 11) % 13;
	const int response = 62 + ((column * 29 + frame * 7) % 55);
	int target = 1 + activeRows * response / 100;
	target += (waveA - 8) * activeRows / 42;
	target += (waveB - 6) * activeRows / 58;
	return clampInt(target, 1, DISPLAY_HEIGHT);
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
	  mTomatoFrame(0),
	  mMicPercent(0),
	  mMicFrame(0),
	  mMicPaletteIndex(0),
	  mMicHasTriggered(false),
	  mMicAutoReportEnabled(false),
	  mIpScrollStartMs(0),
	  mCachedIp("192.168.1.13"),
	  mLastIpQueryMs(0) {
	for (int i = 0; i < MIC_BAR_COUNT; ++i) {
		mMicColumnLevels[i] = 1;
	}
}

FunctionsPage::~FunctionsPage() {
}

void FunctionsPage::onEnter() {
	LOGD_TRACE("FunctionsPage: onEnter");
	mPomodoroLastTickMs = nowMs();
	updateMicReportState();
	MenuIndicator::getInstance().show(FUNC_COUNT, mCurrentFunction);
	draw();
}

void FunctionsPage::onExit() {
	LOGD_TRACE("FunctionsPage: onExit");
	mMiddlePressStartMs = 0;
	mMiddleLongResetDone = false;
	if (mMicAutoReportEnabled) {
		McuManager::getInstance().setAutoMicReport(false);
		mMicAutoReportEnabled = false;
	}
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

bool FunctionsPage::needsFastRefresh() const {
	return mCurrentFunction == FUNC_IP ||
		mCurrentFunction == FUNC_MIC_LIGHT ||
		(mCurrentFunction == FUNC_POMODORO &&
			(mPomodoroRunning || mMiddlePressStartMs > 0));
}

void FunctionsPage::draw() {
	uint64_t currentMs = nowMs();
	updatePomodoro(currentMs);
	updateMiddleLongPress(currentMs);
	Surface surface(DISPLAY_WIDTH, DISPLAY_HEIGHT, COLOR_BLACK);

	if (mCurrentFunction == FUNC_BATTERY) {
		drawBattery(surface);
	} else if (mCurrentFunction == FUNC_IP) {
		drawIpAddress(surface, currentMs);
	} else if (mCurrentFunction == FUNC_MIC_LIGHT) {
		drawMicLight(surface);
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
	if (mCurrentFunction == FUNC_IP) {
		mIpScrollStartMs = nowMs();
	}
	updateMicReportState();
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
	drawTomatoIcon(surface, 1, 1);
	const int minutes = mPomodoroRemainingSec / 60;
	const int seconds = mPomodoroRemainingSec % 60;
	const Color textColor = mPomodoroResting ? COLOR_YELLOW : COLOR_WHITE;
	const Color colonColor = mPomodoroRunning ? COLOR_WHITE : COLOR_YELLOW;
	drawTwoDigitNumber(surface, 15, 13, minutes, textColor);
	Painter::getInstance().drawRect(surface, 31, 5, 2, 2, colonColor, true);
	Painter::getInstance().drawRect(surface, 31, 10, 2, 2, colonColor, true);
	drawTwoDigitNumber(surface, 35, 13, seconds, textColor);
	if (mPomodoroRunning) {
		mTomatoFrame++;
	}
}

void FunctionsPage::drawTomatoIcon(Surface& surface, int x, int y) {
	static const int8_t shine[][2] = {
		{3, 3}, {5, 2}, {8, 3}, {9, 6},
		{7, 9}, {4, 9}, {2, 7}, {2, 4}
	};
	const int frame = mTomatoFrame % 8;

	Painter::getInstance().drawCircle(surface, x + 6, y + 7, 5, COLOR_RED_DEEP, true);
	Painter::getInstance().drawCircle(surface, x + 5, y + 5, 4, COLOR_RED_DARK, true);
	Painter::getInstance().drawCircle(surface, x + 8, y + 5, 4, COLOR_RED_DARK, true);
	Painter::getInstance().drawCircle(surface, x + 4, y + 8, 4, COLOR_RED_DARK, true);
	Painter::getInstance().drawCircle(surface, x + 8, y + 8, 4, COLOR_RED_DARK, true);
	Painter::getInstance().drawCircle(surface, x + 6, y + 6, 5, COLOR_RED, true);

	Painter::getInstance().drawLine(surface, x + 6, y + 6, x + 3, y + 3, COLOR_RED_DARK);
	Painter::getInstance().drawLine(surface, x + 6, y + 6, x + 9, y + 3, COLOR_RED_DARK);
	Painter::getInstance().drawLine(surface, x + 6, y + 6, x + 2, y + 8, COLOR_RED_DARK);
	Painter::getInstance().drawLine(surface, x + 6, y + 6, x + 9, y + 9, COLOR_RED_DARK);

	Painter::getInstance().drawLine(surface, x + 6, y + 6, x + 6, y + 2, COLOR_GREEN);
	Painter::getInstance().drawLine(surface, x + 6, y + 6, x + 3, y + 4, COLOR_GREEN);
	Painter::getInstance().drawLine(surface, x + 6, y + 6, x + 9, y + 4, COLOR_GREEN);
	Painter::getInstance().drawLine(surface, x + 6, y + 6, x + 4, y + 8, COLOR_GREEN_DARK);
	Painter::getInstance().drawLine(surface, x + 6, y + 6, x + 8, y + 8, COLOR_GREEN_DARK);
	Painter::getInstance().drawPixel(surface, x + 6, y + 6, COLOR_YELLOW);

	Painter::getInstance().drawPixel(
		surface,
		x + shine[frame][0],
		y + shine[frame][1],
		COLOR_WHITE);
	if (mPomodoroRunning) {
		Painter::getInstance().drawPixel(
			surface,
			x + shine[(frame + 7) % 8][0],
			y + shine[(frame + 7) % 8][1],
			COLOR_YELLOW);
	}
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

void FunctionsPage::drawIpAddress(Surface& surface, uint64_t currentMs) {
	const std::string ip = currentIpAddress(currentMs);
	const int textWidth = largeTextWidth(ip);
	const int cycleWidth = textWidth + DISPLAY_WIDTH + LARGE_CHAR_GAP;
	int scrollOffset = 0;
	if (cycleWidth > 0) {
		scrollOffset = static_cast<int>(
			((currentMs - mIpScrollStartMs) / IP_SCROLL_SPEED_MS) % cycleWidth);
	}

	int x = DISPLAY_WIDTH - scrollOffset;
	const int y = (DISPLAY_HEIGHT - LARGE_DIGIT_HEIGHT) / 2;
	for (size_t i = 0; i < ip.size(); ++i) {
		x += drawLargeChar(surface, x, y, ip[i], COLOR_WHITE);
		if (i < ip.size() - 1) {
			x += LARGE_CHAR_GAP;
		}
	}
}

void FunctionsPage::drawMicLight(Surface& surface) {
	int raw = McuManager::getInstance().queryMicValue();
	if (raw >= 0) {
		mMicPercent = clampInt(raw * 100 / 65535, 0, 100);
	}
	if (mMicPercent <= MIC_IDLE_PERCENT) {
		mMicPercent = 0;
	} else {
		mMicFrame++;
	}

	const int activeRows = mMicPercent * (DISPLAY_HEIGHT - 1) / 100;
	static const int weights[MIC_BAR_COUNT] = {
		72, 86, 102, 116, 96, 126, 110, 122, 100, 114, 92, 84, 74
	};

	for (int i = 0; i < MIC_BAR_COUNT; ++i) {
		const int x = i * (MIC_BAR_WIDTH + MIC_BAR_GAP);
		int height = 1;
		if (activeRows > 0) {
			const int motion = ((mMicFrame + i * 3) % 5) - 2;
			height += activeRows * (weights[i] + motion * 4) / 100;
			height = clampInt(height, 1, DISPLAY_HEIGHT);
		}

		for (int y = DISPLAY_HEIGHT - height; y < DISPLAY_HEIGHT; ++y) {
			const int rowFromBottom = DISPLAY_HEIGHT - 1 - y;
			Painter::getInstance().drawRect(
				surface, x, y, MIC_BAR_WIDTH, 1,
				micGradientColor(rowFromBottom), true);
		}
	}

	if (mMicPercent == 0) {
		drawMicLightText(surface);
	}
}

void FunctionsPage::drawMicLightText(Surface& surface) {
	drawGradientText(surface, "MIC", 7);
	drawGradientText(surface, "LIGHT", 13);
}

void FunctionsPage::drawGradientText(Surface& surface, const char* text, int y) {
	const int len = static_cast<int>(strlen(text));
	if (len <= 0) {
		return;
	}

	const int width = Painter::getInstance().getTextWidth(text, 0);
	int x = (DISPLAY_WIDTH - width) / 2;
	for (int i = 0; i < len; ++i) {
		const int row = len == 1 ? 0 : i * (DISPLAY_HEIGHT - 1) / (len - 1);
		Painter::getInstance().drawChar(
			surface, x, y, text[i], micGradientColor(row));
		x += Painter::getInstance().getTextWidth(std::string(1, text[i]), 0);
	}
}

void FunctionsPage::updateMicReportState() {
	const bool shouldEnable = mCurrentFunction == FUNC_MIC_LIGHT;
	if (shouldEnable == mMicAutoReportEnabled) {
		return;
	}

	McuManager::getInstance().setAutoMicReport(shouldEnable);
	mMicAutoReportEnabled = shouldEnable;
	if (shouldEnable) {
		mMicPercent = 0;
		mMicFrame = 0;
	}
}

std::string FunctionsPage::currentIpAddress(uint64_t currentMs) {
	if (currentMs - mLastIpQueryMs < IP_QUERY_INTERVAL_MS && !mCachedIp.empty()) {
		return mCachedIp;
	}

	mLastIpQueryMs = currentMs;
	const char* ip = WIFIMANAGER != nullptr ? WIFIMANAGER->getIp() : nullptr;
	if (ip != nullptr && ip[0] != '\0' && std::string(ip) != "0.0.0.0") {
		mCachedIp = ip;
	} else if (mCachedIp.empty()) {
		mCachedIp = "192.168.1.13";
	}
	return mCachedIp;
}

int FunctionsPage::drawLargeChar(Surface& surface, int x, int y, char c,
		const Color& color) {
	if (c >= '0' && c <= '9') {
		drawScaledDigit(surface, x, y, c - '0', LARGE_DIGIT_SCALE, color);
		return LARGE_DIGIT_WIDTH;
	}
	if (c == '.') {
		Painter::getInstance().drawRect(
			surface, x, y + LARGE_DIGIT_HEIGHT - DOT_WIDTH, DOT_WIDTH,
			DOT_WIDTH, color, true);
		return DOT_WIDTH;
	}
	return LARGE_DIGIT_SCALE;
}

int FunctionsPage::largeCharWidth(char c) const {
	if (c >= '0' && c <= '9') {
		return LARGE_DIGIT_WIDTH;
	}
	if (c == '.') {
		return DOT_WIDTH;
	}
	return LARGE_DIGIT_SCALE;
}

int FunctionsPage::largeTextWidth(const std::string& text) const {
	int width = 0;
	for (size_t i = 0; i < text.size(); ++i) {
		width += largeCharWidth(text[i]);
		if (i < text.size() - 1) {
			width += LARGE_CHAR_GAP;
		}
	}
	return width;
}

void FunctionsPage::drawScaledDigit(Surface& surface, int x, int y,
		int digit, int scale, const Color& color) {
	if (digit < 0 || digit > 9 || scale <= 0) {
		return;
	}
	const char* bitmap = DIGITS[digit];
	for (int row = 0; row < 5; ++row) {
		for (int col = 0; col < 3; ++col) {
			if (bitmap[row * 3 + col] != '1') {
				continue;
			}
			Painter::getInstance().drawRect(
				surface, x + col * scale, y + row * scale,
				scale, scale, color, true);
		}
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
	drawScaledDigit(surface, x, y, digit, 2, color);
}
