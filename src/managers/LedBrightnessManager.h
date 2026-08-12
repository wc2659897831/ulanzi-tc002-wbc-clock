#ifndef MANAGERS_LEDBRIGHTNESSMANAGER_H_
#define MANAGERS_LEDBRIGHTNESSMANAGER_H_

#include <stdint.h>
#include <vector>

#include "system/Mutex.h"

class LedBrightnessManager {
public:
	static LedBrightnessManager& getInstance();

	void setBrightness(int brightness);
	int getBrightness();
	void apply(std::vector<uint8_t>& rgbData);

private:
	LedBrightnessManager();
	~LedBrightnessManager();

	LedBrightnessManager(const LedBrightnessManager&) = delete;
	LedBrightnessManager& operator=(const LedBrightnessManager&) = delete;

	Mutex mMutex;
	int mBrightness;
};

#endif
