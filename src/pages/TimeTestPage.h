#ifndef PAGES_TIMETESTPAGE_H_
#define PAGES_TIMETESTPAGE_H_

#include "pages/PageBase.h"
#include "utils/Surface.h"
#include "utils/Painter.h"
#include "base/base.h"
#include <ctime>

namespace time_test_page {
static const Color COLOR_BLACK(0, 0, 0);
static const Color COLOR_TIME(0, 255, 255);
static const Color COLOR_COLON(255, 255, 0);
static const Color COLOR_WEEK_ACTIVE(0, 255, 0);
static const Color COLOR_WEEK_INACTIVE(0, 0, 90);

static const int DISPLAY_WIDTH = 52;
static const int DISPLAY_HEIGHT = 16;
static const int CHINA_UTC_OFFSET_SECONDS = 8 * 60 * 60;
static const int DIGIT_SCALE = 2;
static const int DIGIT_WIDTH = 3 * DIGIT_SCALE;
static const int DIGIT_HEIGHT = 5 * DIGIT_SCALE;
static const int DIGIT_GAP = 1;
static const int COLON_WIDTH = 2;
static const int WEEKDAY_COUNT = 7;
static const int WEEKDAY_GAP = 1;
static const int WEEKDAY_Y = 13;
static const int WEEKDAY_HEIGHT = 3;

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
}

class TimeTestPage : public PageBase {
public:
    TimeTestPage() : PageBase("TimeTestPage") {}
    virtual ~TimeTestPage() {}

    virtual void draw() override {
        Surface surface(
            time_test_page::DISPLAY_WIDTH,
            time_test_page::DISPLAY_HEIGHT,
            time_test_page::COLOR_BLACK);

        time_t now = time(NULL) + time_test_page::CHINA_UTC_OFFSET_SECONDS;
        struct tm timeInfo;
        gmtime_r(&now, &timeInfo);

        int hour = timeInfo.tm_hour;
        int minute = timeInfo.tm_min;
        int second = timeInfo.tm_sec;
        int totalWidth = time_test_page::DIGIT_WIDTH * 6
                       + time_test_page::DIGIT_GAP * 7
                       + time_test_page::COLON_WIDTH * 2;
        int x = (time_test_page::DISPLAY_WIDTH - totalWidth) / 2;
        int y = 1;

        drawDigit(surface, x, y, hour / 10, time_test_page::COLOR_TIME);
        x += time_test_page::DIGIT_WIDTH + time_test_page::DIGIT_GAP;
        drawDigit(surface, x, y, hour % 10, time_test_page::COLOR_TIME);
        x += time_test_page::DIGIT_WIDTH + time_test_page::DIGIT_GAP;
        drawColon(surface, x, y, true, time_test_page::COLOR_COLON);
        x += time_test_page::COLON_WIDTH + time_test_page::DIGIT_GAP;
        drawDigit(surface, x, y, minute / 10, time_test_page::COLOR_TIME);
        x += time_test_page::DIGIT_WIDTH + time_test_page::DIGIT_GAP;
        drawDigit(surface, x, y, minute % 10, time_test_page::COLOR_TIME);
        x += time_test_page::DIGIT_WIDTH + time_test_page::DIGIT_GAP;
        drawColon(surface, x, y, true, time_test_page::COLOR_COLON);
        x += time_test_page::COLON_WIDTH + time_test_page::DIGIT_GAP;
        drawDigit(surface, x, y, second / 10, time_test_page::COLOR_TIME);
        x += time_test_page::DIGIT_WIDTH + time_test_page::DIGIT_GAP;
        drawDigit(surface, x, y, second % 10, time_test_page::COLOR_TIME);

        int weekdayIndex = (timeInfo.tm_wday + 6) % 7;
        drawWeekdayBar(surface, weekdayIndex);

        std::vector<uint8_t> data;
        surface.extractRGB(data);
        sendLedData(data);
    }

    virtual void onEnter() override {
        LOGD_TRACE("TimeTestPage: onEnter");
    }

    virtual void onExit() override {
        LOGD_TRACE("TimeTestPage: onExit");
    }

    virtual bool onKeyEvent(int keyCode, int keyStatus) override {
        return true;
    }

private:
    void drawDigit(Surface& surface, int x, int y, int digit, const Color& color) {
        if (digit < 0 || digit > 9) {
            return;
        }

        const char* bitmap = time_test_page::DIGITS[digit];
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 3; ++col) {
                if (bitmap[row * 3 + col] != '1') {
                    continue;
                }
                Painter::getInstance().drawRect(
                    surface,
                    x + col * time_test_page::DIGIT_SCALE,
                    y + row * time_test_page::DIGIT_SCALE,
                    time_test_page::DIGIT_SCALE,
                    time_test_page::DIGIT_SCALE,
                    color,
                    true);
            }
        }
    }

    void drawColon(Surface& surface, int x, int y, bool visible, const Color& color) {
        if (!visible) {
            return;
        }
        Painter::getInstance().drawRect(
            surface, x, y + 2,
            time_test_page::COLON_WIDTH,
            time_test_page::COLON_WIDTH,
            color, true);
        Painter::getInstance().drawRect(
            surface, x, y + 6,
            time_test_page::COLON_WIDTH,
            time_test_page::COLON_WIDTH,
            color, true);
    }

    void drawWeekdayBar(Surface& surface, int activeIndex) {
        int totalGap = time_test_page::WEEKDAY_GAP * (time_test_page::WEEKDAY_COUNT - 1);
        int baseWidth = (time_test_page::DISPLAY_WIDTH - totalGap) / time_test_page::WEEKDAY_COUNT;
        int remainder = (time_test_page::DISPLAY_WIDTH - totalGap) % time_test_page::WEEKDAY_COUNT;
        int x = 0;

        for (int i = 0; i < time_test_page::WEEKDAY_COUNT; ++i) {
            int width = baseWidth + (i < remainder ? 1 : 0);
            bool active = (i == activeIndex);
            Painter::getInstance().drawRect(
                surface,
                x,
                time_test_page::WEEKDAY_Y,
                width,
                time_test_page::WEEKDAY_HEIGHT,
                active ? time_test_page::COLOR_WEEK_ACTIVE : time_test_page::COLOR_WEEK_INACTIVE,
                active);
            x += width + time_test_page::WEEKDAY_GAP;
        }
    }
};

#endif
