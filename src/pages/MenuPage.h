#ifndef PAGES_MENUPAGE_H_
#define PAGES_MENUPAGE_H_

#include "pages/PageBase.h"
#include "utils/Painter.h"
#include "utils/Surface.h"

class MenuPage : public PageBase {
public:
	enum TopPage {
		TOP_CLOCK = 0,
		TOP_FUNCTIONS,
		TOP_STREAM,
		TOP_SETTINGS,
		TOP_TEST,
		TOP_COUNT
	};

	MenuPage();
	virtual ~MenuPage();

	virtual void draw() override;
	virtual void onEnter() override;
	virtual void onExit() override;
	virtual bool onKeyEvent(int keyCode, int keyStatus) override;

	void setTopPage(int topPage);
	int getTopPage() const;
	void setClockCountdown(int countdown);

private:
	int mTopPage;
	int mClockCountdown;

	void drawTestMenu(Surface& surface);
	void drawClockMenu(Surface& surface);
	void drawFunctionsMenu(Surface& surface);
	void drawStreamMenu(Surface& surface);
	void drawSettingsMenu(Surface& surface);
	void drawCenteredText(Surface& surface, const char* text, int y,
			const Color& color);
	void drawCountdown(Surface& surface);
	void drawFunctionsIcon(Surface& surface);
	void drawStreamIcon(Surface& surface);
	void drawSettingsIcon(Surface& surface);
};

#endif
