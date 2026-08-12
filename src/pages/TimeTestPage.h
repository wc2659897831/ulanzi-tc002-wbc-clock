#ifndef PAGES_TIMETESTPAGE_H_
#define PAGES_TIMETESTPAGE_H_

#include "pages/PageBase.h"
#include "utils/Surface.h"
#include "utils/Painter.h"
#include "base/base.h"
#include <ctime>
#include <cstdlib>
#include <stdint.h>
#include <sys/time.h>

namespace time_test_page {
static const Color COLOR_BLACK(0, 0, 0);
static const Color COLOR_WHITE(255, 255, 255);
static const Color COLOR_WEEK_DIM(55, 55, 55);

static const int DISPLAY_WIDTH = 52;
static const int DISPLAY_HEIGHT = 16;
static const int CHINA_UTC_OFFSET_SECONDS = 8 * 60 * 60;
static const int DIGIT_SCALE = 2;
static const int DIGIT_WIDTH = 3 * DIGIT_SCALE;
static const int DIGIT_HEIGHT = 5 * DIGIT_SCALE;
static const int DIGIT_GAP = 1;
static const int COLON_WIDTH = 1;
static const int WEEKDAY_COUNT = 7;
static const int WEEKDAY_DOT_W = 6;
static const int WEEKDAY_DOT_H = 2;
static const int HOURLY_FLASH_MS = 3000;
static const int HOURLY_FLASH_INTERVAL_MS = 500;

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
    TimeTestPage()
        : PageBase("TimeTestPage"),
          mLastHourlyKey(-1),
          mHourlyFlashStartMs(0),
          mHourlyFlashSeed(0) {}
    virtual ~TimeTestPage() {}

    virtual void draw() override {
        Surface surface(
            time_test_page::DISPLAY_WIDTH,
            time_test_page::DISPLAY_HEIGHT,
            time_test_page::COLOR_BLACK);

        uint64_t rawNowMs = nowMs();
        time_t rawNow = static_cast<time_t>(rawNowMs / 1000);
        time_t now = rawNow + time_test_page::CHINA_UTC_OFFSET_SECONDS;
        struct tm timeInfo;
        gmtime_r(&now, &timeInfo);

        updateHourlyFlash(timeInfo, rawNowMs);
        const Color timeColor = currentTimeColor(rawNowMs);

        int hour = timeInfo.tm_hour;
        int minute = timeInfo.tm_min;
        int second = timeInfo.tm_sec;
        int totalWidth = time_test_page::DIGIT_WIDTH * 6
                       + time_test_page::DIGIT_GAP * 5
                       + time_test_page::COLON_WIDTH * 2;
        int x = (time_test_page::DISPLAY_WIDTH - totalWidth) / 2;
        int y = 2;

        drawDigit(surface, x, y, hour / 10, timeColor);
        x += time_test_page::DIGIT_WIDTH + time_test_page::DIGIT_GAP;
        drawDigit(surface, x, y, hour % 10, timeColor);
        x += time_test_page::DIGIT_WIDTH + time_test_page::DIGIT_GAP;
        drawColon(surface, x, y, true, timeColor);
        x += time_test_page::COLON_WIDTH + time_test_page::DIGIT_GAP;
        drawDigit(surface, x, y, minute / 10, timeColor);
        x += time_test_page::DIGIT_WIDTH + time_test_page::DIGIT_GAP;
        drawDigit(surface, x, y, minute % 10, timeColor);
        x += time_test_page::DIGIT_WIDTH + time_test_page::DIGIT_GAP;
        drawColon(surface, x, y, true, timeColor);
        x += time_test_page::COLON_WIDTH + time_test_page::DIGIT_GAP;
        drawDigit(surface, x, y, second / 10, timeColor);
        x += time_test_page::DIGIT_WIDTH + time_test_page::DIGIT_GAP;
        drawDigit(surface, x, y, second % 10, timeColor);

        int weekdayIndex = (timeInfo.tm_wday + 6) % 7;
        drawWeekdayDots(surface, weekdayIndex);

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
        (void) keyCode;
        (void) keyStatus;
        return true;
    }

private:
    int mLastHourlyKey;
    uint64_t mHourlyFlashStartMs;
    uint32_t mHourlyFlashSeed;

    uint64_t nowMs() const {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        return static_cast<uint64_t>(tv.tv_sec) * 1000ULL +
            static_cast<uint64_t>(tv.tv_usec) / 1000ULL;
    }

    void updateHourlyFlash(const struct tm& timeInfo, uint64_t rawNowMs) {
        if (timeInfo.tm_min != 0 || timeInfo.tm_sec != 0) {
            return;
        }

        int hourlyKey = (timeInfo.tm_yday * 24) + timeInfo.tm_hour;
        if (hourlyKey == mLastHourlyKey) {
            return;
        }

        mLastHourlyKey = hourlyKey;
        mHourlyFlashStartMs = rawNowMs;
        mHourlyFlashSeed = static_cast<uint32_t>(
            rawNowMs ^ (static_cast<uint64_t>(hourlyKey) * 1103515245ULL));
    }

    Color currentTimeColor(uint64_t rawNowMs) {
        if (mHourlyFlashStartMs <= 0) {
            return time_test_page::COLOR_WHITE;
        }

        int elapsedMs = static_cast<int>(rawNowMs - mHourlyFlashStartMs);
        if (elapsedMs < 0 || elapsedMs >= time_test_page::HOURLY_FLASH_MS) {
            return time_test_page::COLOR_WHITE;
        }

        int flashIndex = elapsedMs / time_test_page::HOURLY_FLASH_INTERVAL_MS;
        return randomFlashColor(flashIndex);
    }

    Color randomFlashColor(int index) {
        static const Color colors[] = {
            Color(255, 60, 60),
            Color(255, 210, 60),
            Color(60, 255, 120),
            Color(60, 220, 255),
            Color(170, 90, 255),
            Color(255, 80, 210)
        };
        const int count = sizeof(colors) / sizeof(colors[0]);
        uint32_t colorIndex = mHourlyFlashSeed
            + static_cast<uint32_t>(index * 1664525 + 1013904223);
        return colors[colorIndex % count];
    }

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
        Painter::getInstance().drawRect(surface, x, y + 3, 1, 1, color, true);
        Painter::getInstance().drawRect(surface, x, y + 7, 1, 1, color, true);
    }

    void drawWeekdayDots(Surface& surface, int activeIndex) {
        const int gap = 1;
        const int totalWidth = time_test_page::WEEKDAY_COUNT
                             * time_test_page::WEEKDAY_DOT_W
                             + (time_test_page::WEEKDAY_COUNT - 1) * gap;
        const int startX = (time_test_page::DISPLAY_WIDTH - totalWidth) / 2;
        const int y = time_test_page::DISPLAY_HEIGHT - time_test_page::WEEKDAY_DOT_H;

        for (int i = 0; i < time_test_page::WEEKDAY_COUNT; ++i) {
            int x = startX + i * (time_test_page::WEEKDAY_DOT_W + gap);
            bool active = (i == activeIndex);
            Painter::getInstance().drawRect(
                surface,
                x,
                y,
                time_test_page::WEEKDAY_DOT_W,
                time_test_page::WEEKDAY_DOT_H,
                active ? time_test_page::COLOR_WHITE : time_test_page::COLOR_WEEK_DIM,
                true);
        }
    }
};

#endif
