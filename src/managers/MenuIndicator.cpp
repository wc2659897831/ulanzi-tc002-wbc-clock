#include "managers/MenuIndicator.h"

namespace {

#define DISPLAY_WIDTH 52
#define DISPLAY_HEIGHT 16
#define DOT_X (DISPLAY_WIDTH - 1)
#define DOT_COLOR 255
#define DOT_DIM 55
#define MAX_MENU_COUNT 5

} // namespace

MenuIndicator& MenuIndicator::getInstance() {
	static MenuIndicator instance;
	return instance;
}

MenuIndicator::MenuIndicator()
	: mVisible(false),
	  mCount(0),
	  mActiveIndex(0) {
}

MenuIndicator::~MenuIndicator() {
}

void MenuIndicator::show(int count, int activeIndex) {
	Mutex::Autolock lock(mMutex);
	if (count < 0) {
		count = 0;
	}
	if (count > MAX_MENU_COUNT) {
		count = MAX_MENU_COUNT;
	}
	if (activeIndex < 0) {
		activeIndex = 0;
	}
	if (activeIndex >= count && count > 0) {
		activeIndex = count - 1;
	}
	mVisible = count > 1;
	mCount = count;
	mActiveIndex = activeIndex;
}

void MenuIndicator::hide() {
	Mutex::Autolock lock(mMutex);
	mVisible = false;
}

void MenuIndicator::apply(std::vector<uint8_t>& rgbData) {
	Mutex::Autolock lock(mMutex);
	if (!mVisible || mCount <= 0 ||
			rgbData.size() < DISPLAY_WIDTH * DISPLAY_HEIGHT * 3) {
		return;
	}

	const int step = mCount > 4 ? 3 : 4;
	const int totalHeight = (mCount - 1) * step + 1;
	const int startY = (DISPLAY_HEIGHT - totalHeight) / 2;

	for (int i = 0; i < mCount; ++i) {
		const int y = startY + i * step;
		const bool active = (i == mActiveIndex);
		const uint8_t value = active ? DOT_COLOR : DOT_DIM;

		setPixel(rgbData, DOT_X, y, value, value, value);
	}
}

void MenuIndicator::setPixel(std::vector<uint8_t>& rgbData, int x, int y,
		uint8_t r, uint8_t g, uint8_t b) {
	if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) {
		return;
	}
	const int index = (y * DISPLAY_WIDTH + x) * 3;
	rgbData[index] = r;
	rgbData[index + 1] = g;
	rgbData[index + 2] = b;
}
