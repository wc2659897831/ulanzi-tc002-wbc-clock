#include "pages/MenuPage.h"

#include "base/base.h"

namespace {

static const Color COLOR_BLACK(0, 0, 0);
static const Color COLOR_WHITE(255, 255, 255);
static const Color COLOR_CYAN(0, 220, 255);
static const Color COLOR_GREEN(0, 255, 120);
static const Color COLOR_YELLOW(255, 210, 60);
static const Color COLOR_RED(255, 60, 50);
static const Color COLOR_MAGENTA(255, 80, 210);
static const Color COLOR_DIM(40, 40, 40);

#define DISPLAY_WIDTH 52
#define DISPLAY_HEIGHT 16

} // namespace

MenuPage::MenuPage()
	: PageBase("MenuPage"),
	  mTopPage(TOP_CLOCK),
	  mClockCountdown(0) {
}

MenuPage::~MenuPage() {
}

void MenuPage::onEnter() {
	LOGD_TRACE("MenuPage: onEnter");
}

void MenuPage::onExit() {
	LOGD_TRACE("MenuPage: onExit");
}

bool MenuPage::onKeyEvent(int keyCode, int keyStatus) {
	(void) keyCode;
	(void) keyStatus;
	return true;
}

void MenuPage::setTopPage(int topPage) {
	if (topPage < 0) {
		topPage = TOP_COUNT - 1;
	}
	if (topPage >= TOP_COUNT) {
		topPage = 0;
	}
	mTopPage = topPage;
}

int MenuPage::getTopPage() const {
	return mTopPage;
}

void MenuPage::setClockCountdown(int countdown) {
	if (countdown < 0) {
		countdown = 0;
	}
	if (countdown > 3) {
		countdown = 3;
	}
	mClockCountdown = countdown;
}

void MenuPage::draw() {
	Surface surface(DISPLAY_WIDTH, DISPLAY_HEIGHT, COLOR_BLACK);

	switch (mTopPage) {
	case TOP_CLOCK:
		drawClockMenu(surface);
		break;
	case TOP_FUNCTIONS:
		drawFunctionsMenu(surface);
		break;
	case TOP_STREAM:
		drawStreamMenu(surface);
		break;
	case TOP_SETTINGS:
		drawSettingsMenu(surface);
		break;
	case TOP_TEST:
	default:
		drawTestMenu(surface);
		break;
	}

	std::vector<uint8_t> data;
	surface.extractRGB(data);
	sendLedData(data);
}

void MenuPage::drawTestMenu(Surface& surface) {
	drawCenteredText(surface, "TEST", 8, COLOR_WHITE);
	drawCenteredText(surface, "PAGE", 14, COLOR_CYAN);
}

void MenuPage::drawClockMenu(Surface& surface) {
	const int cx = 12;
	const int cy = 8;

	Painter::getInstance().drawCircle(surface, cx, cy, 6, COLOR_CYAN, false);
	Painter::getInstance().drawRect(surface, cx + 4, cy - 2, 4, 5, COLOR_BLACK, true);
	Painter::getInstance().drawLine(surface, cx, cy, cx, cy - 4, COLOR_WHITE);
	Painter::getInstance().drawLine(surface, cx, cy, cx + 4, cy, COLOR_WHITE);
	Painter::getInstance().drawPixel(surface, cx, cy, COLOR_GREEN);
	Painter::getInstance().drawText(surface, 21, 11, "CLOCK", COLOR_WHITE, 0);
	drawCountdown(surface);
}

void MenuPage::drawFunctionsMenu(Surface& surface) {
	drawFunctionsIcon(surface);
	drawCenteredText(surface, "FUNC", 8, COLOR_WHITE);
	drawCenteredText(surface, "PAGE", 14, COLOR_YELLOW);
}

void MenuPage::drawStreamMenu(Surface& surface) {
	drawStreamIcon(surface);
	drawCenteredText(surface, "PUSH", 8, COLOR_WHITE);
	drawCenteredText(surface, "IMG", 14, COLOR_MAGENTA);
}

void MenuPage::drawSettingsMenu(Surface& surface) {
	drawSettingsIcon(surface);
	drawCenteredText(surface, "SET", 8, COLOR_WHITE);
	drawCenteredText(surface, "TINGS", 14, COLOR_GREEN);
}

void MenuPage::drawCenteredText(Surface& surface, const char* text, int y,
		const Color& color) {
	const int width = Painter::getInstance().getTextWidth(text, 0);
	const int x = (DISPLAY_WIDTH - width) / 2;
	Painter::getInstance().drawText(surface, x, y, text, color, 0);
}

void MenuPage::drawCountdown(Surface& surface) {
	if (mClockCountdown <= 0) {
		return;
	}

	Color color = COLOR_GREEN;
	if (mClockCountdown == 2) {
		color = COLOR_YELLOW;
	} else if (mClockCountdown == 1) {
		color = COLOR_RED;
	}

	char text[2] = { static_cast<char>('0' + mClockCountdown), '\0' };
	const int clockTextX = 21;
	const int clockRightX = clockTextX +
		Painter::getInstance().getTextWidth("CLOCK", 0) - 1;
	const int digitWidth = Painter::getInstance().getTextWidth(text, 0);
	const int centerX = (clockRightX + DISPLAY_WIDTH - 1) / 2;
	const int x = centerX - digitWidth / 2;
	Painter::getInstance().drawText(surface, x, 11, text, color, 0);
}

void MenuPage::drawFunctionsIcon(Surface& surface) {
	Painter::getInstance().drawRect(surface, 4, 3, 11, 10, COLOR_DIM, false);
	Painter::getInstance().drawRect(surface, 6, 5, 3, 3, COLOR_RED, true);
	Painter::getInstance().drawRect(surface, 10, 5, 3, 3, COLOR_GREEN, true);
	Painter::getInstance().drawLine(surface, 7, 11, 12, 11, COLOR_YELLOW);
}

void MenuPage::drawStreamIcon(Surface& surface) {
	Painter::getInstance().drawRect(surface, 3, 4, 10, 8, COLOR_DIM, false);
	Painter::getInstance().drawRect(surface, 5, 6, 6, 4, COLOR_MAGENTA, true);
	Painter::getInstance().drawPixel(surface, 16, 6, COLOR_WHITE);
	Painter::getInstance().drawPixel(surface, 18, 8, COLOR_WHITE);
	Painter::getInstance().drawPixel(surface, 16, 10, COLOR_WHITE);
}

void MenuPage::drawSettingsIcon(Surface& surface) {
	Painter::getInstance().drawLine(surface, 4, 5, 15, 5, COLOR_DIM);
	Painter::getInstance().drawLine(surface, 4, 9, 15, 9, COLOR_DIM);
	Painter::getInstance().drawLine(surface, 4, 13, 15, 13, COLOR_DIM);
	Painter::getInstance().drawRect(surface, 7, 4, 2, 3, COLOR_GREEN, true);
	Painter::getInstance().drawRect(surface, 12, 8, 2, 3, COLOR_CYAN, true);
	Painter::getInstance().drawRect(surface, 5, 12, 2, 3, COLOR_MAGENTA, true);
}
