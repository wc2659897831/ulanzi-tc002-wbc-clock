#ifndef PAGES_FUNCTIONSPAGE_H_
#define PAGES_FUNCTIONSPAGE_H_

#include "pages/PageBase.h"
#include "utils/Painter.h"
#include "utils/Surface.h"
#include <stdint.h>
#include <string>

class FunctionsPage : public PageBase {
public:
	FunctionsPage();
	virtual ~FunctionsPage();

	virtual void draw() override;
	virtual void onEnter() override;
	virtual void onExit() override;
	virtual bool onKeyEvent(int keyCode, int keyStatus) override;
	bool needsFastRefresh() const;

private:
	enum FunctionItem {
		FUNC_POMODORO = 0,
		FUNC_BATTERY,
		FUNC_IP,
		FUNC_COUNT
	};

	int mCurrentFunction;
	bool mPomodoroRunning;
	bool mPomodoroResting;
	int mPomodoroRemainingSec;
	uint64_t mPomodoroLastTickMs;
	uint64_t mMiddlePressStartMs;
	bool mMiddleLongResetDone;
	int mChargeFrame;
	int mTomatoFrame;
	uint64_t mIpScrollStartMs;
	std::string mCachedIp;
	uint64_t mLastIpQueryMs;

	uint64_t nowMs() const;
	void switchFunction(int delta);
	void togglePomodoro();
	void resetPomodoro();
	void advancePomodoroPhase();
	void updatePomodoro(uint64_t currentMs);
	void updateMiddleLongPress(uint64_t currentMs);
	void drawPomodoro(Surface& surface);
	void drawTomatoIcon(Surface& surface, int x, int y);
	void drawBattery(Surface& surface);
	void drawBatteryIcon(Surface& surface, int x, int y, int percent, bool charging);
	void drawIpAddress(Surface& surface, uint64_t currentMs);
	std::string currentIpAddress(uint64_t currentMs);
	int drawLargeChar(Surface& surface, int x, int y, char c, const Color& color);
	void drawScaledDigit(Surface& surface, int x, int y, int digit,
			int scale, const Color& color);
	int largeCharWidth(char c) const;
	int largeTextWidth(const std::string& text) const;
	void drawHeader(Surface& surface, const char* text);
	void drawTwoDigitNumber(Surface& surface, int x, int y, int value, const Color& color);
	void drawDigit(Surface& surface, int x, int y, int digit, const Color& color);
};

#endif
