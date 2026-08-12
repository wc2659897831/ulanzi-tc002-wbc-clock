#include "pages/SettingsPage.h"

#include "base/base.h"
#include "managers/AudioManager.h"
#include "managers/LedBrightnessManager.h"

namespace {

static const Color COLOR_BLACK(0, 0, 0);
static const Color COLOR_WHITE(255, 255, 255);
static const Color COLOR_DIM(45, 45, 45);
static const Color COLOR_VOLUME(0, 220, 255);
static const Color COLOR_BRIGHTNESS(255, 210, 70);

#define DISPLAY_WIDTH 52
#define DISPLAY_HEIGHT 16

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

SettingsPage::SettingsPage()
	: PageBase("SettingsPage"),
	  mCurrentItem(ITEM_VOLUME),
	  mVolumeLevel(3),
	  mBrightness(80) {
}

SettingsPage::~SettingsPage() {
}

void SettingsPage::onEnter() {
	LOGD_TRACE("SettingsPage: onEnter");
	mBrightness = LedBrightnessManager::getInstance().getBrightness();
	draw();
}

void SettingsPage::onExit() {
	LOGD_TRACE("SettingsPage: onExit");
}

bool SettingsPage::onKeyEvent(int keyCode, int keyStatus) {
	if (keyCode == E_KEYCODE_CLOCKWISE && keyStatus == 0) {
		adjustCurrent(1);
		return false;
	}

	if (keyCode == E_KEYCODE_ANTI_CLOCKWISE && keyStatus == 0) {
		adjustCurrent(-1);
		return false;
	}

	if (keyStatus != 1) {
		return true;
	}

	switch (keyCode) {
	case E_KEYCODE_LEFT_BUTTON:
		switchItem(-1);
		return false;
	case E_KEYCODE_RIGHT_BUTTON:
		switchItem(1);
		return false;
	case E_KEYCODE_MIDDLE_BUTTON:
		switchItem(1);
		return false;
	default:
		return true;
	}
}

void SettingsPage::draw() {
	Surface surface(DISPLAY_WIDTH, DISPLAY_HEIGHT, COLOR_BLACK);

	drawRow(surface, 4, "VOL", mVolumeLevel, 6,
		mCurrentItem == ITEM_VOLUME, COLOR_VOLUME);
	drawRow(surface, 12, "BRI", mBrightness, 100,
		mCurrentItem == ITEM_BRIGHTNESS, COLOR_BRIGHTNESS);

	std::vector<uint8_t> data;
	surface.extractRGB(data);
	sendLedData(data);
}

void SettingsPage::adjustCurrent(int delta) {
	if (mCurrentItem == ITEM_VOLUME) {
		mVolumeLevel = clampInt(mVolumeLevel + delta, 0, 6);
		applyVolume();
	} else {
		mBrightness = clampInt(mBrightness + delta * 5, 0, 100);
		applyBrightness();
	}
	draw();
}

void SettingsPage::switchItem(int delta) {
	mCurrentItem += delta;
	while (mCurrentItem < 0) {
		mCurrentItem += ITEM_COUNT;
	}
	while (mCurrentItem >= ITEM_COUNT) {
		mCurrentItem -= ITEM_COUNT;
	}
	draw();
}

void SettingsPage::applyVolume() {
	awtrix::AudioManager::getInstance().setVolume(mVolumeLevel);
}

void SettingsPage::applyBrightness() {
	LedBrightnessManager::getInstance().setBrightness(mBrightness);
}

void SettingsPage::drawRow(Surface& surface, int y, const char* label, int value,
		int maxValue, bool active, const Color& activeColor) {
	const Color& textColor = active ? activeColor : COLOR_WHITE;
	Painter::getInstance().drawText(surface, 0, y, label, textColor, 0);

	const int barX = 18;
	const int barY = y - 4;
	const int barW = 31;
	const int barH = 4;
	const int fillW = maxValue > 0 ? value * barW / maxValue : 0;

	Painter::getInstance().drawRect(surface, barX, barY, barW, barH, COLOR_DIM, false);
	if (fillW > 0) {
		Painter::getInstance().drawRect(surface, barX, barY, fillW, barH,
			activeColor, true);
	}
	if (active) {
		Painter::getInstance().drawPixel(surface, 50, barY + 1, activeColor);
	}
}
