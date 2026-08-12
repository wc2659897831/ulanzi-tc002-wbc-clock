#ifndef PAGES_SETTINGSPAGE_H_
#define PAGES_SETTINGSPAGE_H_

#include "pages/PageBase.h"
#include "utils/Painter.h"
#include "utils/Surface.h"

class SettingsPage : public PageBase {
public:
	SettingsPage();
	virtual ~SettingsPage();

	virtual void draw() override;
	virtual void onEnter() override;
	virtual void onExit() override;
	virtual bool onKeyEvent(int keyCode, int keyStatus) override;

private:
	enum SettingItem {
		ITEM_VOLUME = 0,
		ITEM_BRIGHTNESS,
		ITEM_COUNT
	};

	int mCurrentItem;
	int mVolumeLevel;
	int mBrightness;

	void adjustCurrent(int delta);
	void switchItem(int delta);
	void applyVolume();
	void applyBrightness();
	void drawRow(Surface& surface, int y, const char* label, int value,
			int maxValue, bool active, const Color& activeColor);
};

#endif
