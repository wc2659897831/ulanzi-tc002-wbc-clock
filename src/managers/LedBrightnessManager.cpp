#include "managers/LedBrightnessManager.h"

LedBrightnessManager& LedBrightnessManager::getInstance() {
	static LedBrightnessManager instance;
	return instance;
}

LedBrightnessManager::LedBrightnessManager()
	: mBrightness(80) {
}

LedBrightnessManager::~LedBrightnessManager() {
}

void LedBrightnessManager::setBrightness(int brightness) {
	if (brightness < 0) {
		brightness = 0;
	} else if (brightness > 100) {
		brightness = 100;
	}

	Mutex::Autolock lock(mMutex);
	mBrightness = brightness;
}

int LedBrightnessManager::getBrightness() {
	Mutex::Autolock lock(mMutex);
	return mBrightness;
}

void LedBrightnessManager::apply(std::vector<uint8_t>& rgbData) {
	int brightness = 0;
	{
		Mutex::Autolock lock(mMutex);
		brightness = mBrightness;
	}

	if (brightness >= 100) {
		return;
	}

	for (size_t i = 0; i < rgbData.size(); ++i) {
		rgbData[i] = static_cast<uint8_t>(
			static_cast<int>(rgbData[i]) * brightness / 100);
	}
}
