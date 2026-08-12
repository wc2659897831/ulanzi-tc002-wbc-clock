#ifndef MANAGERS_MENUINDICATOR_H_
#define MANAGERS_MENUINDICATOR_H_

#include <stdint.h>
#include <vector>

#include "system/Mutex.h"

class MenuIndicator {
public:
	static MenuIndicator& getInstance();

	void show(int count, int activeIndex);
	void hide();
	void apply(std::vector<uint8_t>& rgbData);

private:
	MenuIndicator();
	~MenuIndicator();

	MenuIndicator(const MenuIndicator&) = delete;
	MenuIndicator& operator=(const MenuIndicator&) = delete;

	void setPixel(std::vector<uint8_t>& rgbData, int x, int y,
			uint8_t r, uint8_t g, uint8_t b);

	Mutex mMutex;
	bool mVisible;
	int mCount;
	int mActiveIndex;
};

#endif
