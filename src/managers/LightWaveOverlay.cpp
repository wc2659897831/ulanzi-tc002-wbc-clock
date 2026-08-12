#include "managers/LightWaveOverlay.h"

#include <sys/time.h>
#include <unistd.h>

#include "base/log.h"
#include "pages/PageBase.h"
#include "system/Thread.h"

namespace {

#define DISPLAY_WIDTH 52
#define DISPLAY_HEIGHT 16
#define WAVE_FRAME_COUNT 18
#define WAVE_FRAME_INTERVAL_US (60 * 1000)
#define MAX_ACTIVE_WAVES 4
#define DIAGONAL_BAR_MARGIN 320

} // namespace

class LightWaveOverlay::AnimationThread : public Thread {
public:
	AnimationThread(LightWaveOverlay* overlay) : mOverlay(overlay) {}

protected:
	virtual bool threadLoop() override {
		return mOverlay->animationThreadLoop();
	}

private:
	LightWaveOverlay* mOverlay;
};

LightWaveOverlay::LightWaveOverlay()
	: mRandomSeed(0),
	  mThreadRunning(false),
	  mThread(nullptr) {
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	mRandomSeed = static_cast<uint32_t>(tv.tv_sec ^ tv.tv_usec);
}

LightWaveOverlay::~LightWaveOverlay() {
	if (mThread) {
		delete mThread;
		mThread = nullptr;
	}
}

LightWaveOverlay& LightWaveOverlay::getInstance() {
	static LightWaveOverlay instance;
	return instance;
}

void LightWaveOverlay::trigger(Direction direction) {
	Mutex::Autolock lock(mMutex);
	if (mWaves.size() >= MAX_ACTIVE_WAVES) {
		mWaves.erase(mWaves.begin());
	}
	mWaves.push_back(createWave(direction));

	if (!mThreadRunning) {
		mThreadRunning = true;
		if (mThread) {
			delete mThread;
		}
		mThread = new AnimationThread(this);
		if (!mThread->run("light_wave_overlay")) {
			LOGE_TRACE("LightWaveOverlay: failed to start animation thread");
			delete mThread;
			mThread = nullptr;
			mThreadRunning = false;
			mWaves.clear();
		}
	}
}

void LightWaveOverlay::triggerDiagonal(DiagonalDirection direction) {
	Mutex::Autolock lock(mMutex);
	if (mWaves.size() >= MAX_ACTIVE_WAVES) {
		mWaves.erase(mWaves.begin());
	}
	mWaves.push_back(createDiagonalWave(direction));

	if (!mThreadRunning) {
		mThreadRunning = true;
		if (mThread) {
			delete mThread;
		}
		mThread = new AnimationThread(this);
		if (!mThread->run("light_wave_overlay")) {
			LOGE_TRACE("LightWaveOverlay: failed to start animation thread");
			delete mThread;
			mThread = nullptr;
			mThreadRunning = false;
			mWaves.clear();
		}
	}
}

LightWaveOverlay::WaveState LightWaveOverlay::createWave(Direction direction) {
	WaveState wave;
	wave.type = WAVE_RADIAL;
	wave.direction = direction;
	wave.diagonalDirection = DIAGONAL_LEFT_TOP_TO_RIGHT_BOTTOM;
	wave.frame = 0;

	const Color palette[] = {
		Color(85, 245, 255),
		Color(90, 150, 255),
		Color(185, 105, 255),
		Color(255, 95, 190),
		Color(255, 125, 70),
		Color(255, 220, 70),
		Color(80, 255, 135)
	};
	const int paletteCount = sizeof(palette) / sizeof(palette[0]);
	Color base = palette[nextRandom() % paletteCount];

	wave.spark = Color(255, 255, 238);
	wave.core = Color(
		255,
		220 + base.g / 8,
		190 + base.b / 6);
	wave.inner = base;
	wave.outer = Color(base.r * 2 / 5, base.g * 2 / 5, base.b);
	wave.glow = Color(base.r / 12, base.g / 12, base.b / 5);
	return wave;
}

LightWaveOverlay::WaveState LightWaveOverlay::createDiagonalWave(DiagonalDirection direction) {
	WaveState wave = createWave(DIRECTION_FROM_TOP);
	wave.type = WAVE_DIAGONAL_BAR;
	wave.diagonalDirection = direction;
	return wave;
}

uint32_t LightWaveOverlay::nextRandom() {
	mRandomSeed = mRandomSeed * 1664525u + 1013904223u;
	return mRandomSeed;
}

void LightWaveOverlay::updateBaseFrame(const std::vector<uint8_t>& rgbData) {
	Mutex::Autolock lock(mMutex);
	mBaseFrame = rgbData;
}

void LightWaveOverlay::apply(std::vector<uint8_t>& rgbData) {
	Mutex::Autolock lock(mMutex);
	if (!mWaves.empty()) {
		overlayWaves(rgbData);
	}
}

bool LightWaveOverlay::animationThreadLoop() {
	std::vector<uint8_t> frameData;

	{
		Mutex::Autolock lock(mMutex);
		if (mWaves.empty() || mBaseFrame.empty()) {
			mThreadRunning = false;
			return false;
		}
		frameData = mBaseFrame;
		overlayWaves(frameData);
		advanceWaves();
	}

	sendFrame(frameData);
	usleep(WAVE_FRAME_INTERVAL_US);

	{
		Mutex::Autolock lock(mMutex);
		if (mWaves.empty()) {
			mThreadRunning = false;
			return false;
		}
	}
	return true;
}

void LightWaveOverlay::overlayWaves(std::vector<uint8_t>& rgbData) {
	for (std::vector<WaveState>::const_iterator it = mWaves.begin();
		 it != mWaves.end(); ++it) {
		overlayWave(rgbData, *it);
	}
}

void LightWaveOverlay::overlayWave(std::vector<uint8_t>& rgbData, const WaveState& wave) {
	if (wave.type == WAVE_DIAGONAL_BAR) {
		overlayDiagonalBar(rgbData, wave);
		return;
	}
	overlayRadialWave(rgbData, wave);
}

void LightWaveOverlay::overlayRadialWave(std::vector<uint8_t>& rgbData, const WaveState& wave) {
	if (rgbData.size() < DISPLAY_WIDTH * DISPLAY_HEIGHT * 3) {
		return;
	}

	int originX2 = 0;
	int originY2 = 0;
	originForDirection(wave.direction, originX2, originY2);

	const int maxRadiusSq4 = maxRadiusSq4FromOrigin(originX2, originY2);
	const int progress = (wave.frame * 100) / (WAVE_FRAME_COUNT - 1);
	const int radiusSq4 = maxRadiusSq4 * progress * progress / 10000;
	const int fade = 100 - (wave.frame * 36) / (WAVE_FRAME_COUNT - 1);

	for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
		for (int x = 0; x < DISPLAY_WIDTH; ++x) {
			const int dx2 = x * 2 - originX2;
			const int dy2 = y * 2 - originY2;
			const int distSq4 = dx2 * dx2 + dy2 * dy2;
			int delta = distSq4 - radiusSq4;
			if (delta < 0) {
				delta = -delta;
			}

			Color color;
			int strength = 0;
			if (delta <= 40) {
				color = wave.core;
				strength = 100;
			} else if (delta <= 130) {
				color = wave.inner;
				strength = 78;
			} else if (delta <= 250) {
				color = wave.outer;
				strength = 46;
			} else if (delta <= 430) {
				color = wave.glow;
				strength = 25;
			}

			if (strength > 0) {
				setPixelAdd(rgbData, x, y, color, strength * fade, 10000);
			}
		}
	}

	if (wave.frame < 4) {
		int spark = 100 - wave.frame * 18;
		setPixelAdd(rgbData, originX2 / 2, originY2 / 2, wave.spark, spark, 100);
		if (wave.direction == DIRECTION_FROM_BOTTOM || wave.direction == DIRECTION_FROM_TOP) {
			setPixelAdd(rgbData, originX2 / 2 - 1, originY2 / 2, wave.spark, spark, 100);
		} else {
			setPixelAdd(rgbData, originX2 / 2, originY2 / 2 - 1, wave.spark, spark, 100);
		}
	}
}

void LightWaveOverlay::overlayDiagonalBar(std::vector<uint8_t>& rgbData, const WaveState& wave) {
	if (rgbData.size() < DISPLAY_WIDTH * DISPLAY_HEIGHT * 3) {
		return;
	}

	int minAxis = 0;
	int maxAxis = 0;
	if (wave.diagonalDirection == DIAGONAL_LEFT_TOP_TO_RIGHT_BOTTOM) {
		minAxis = -(DISPLAY_WIDTH - 1) * (DISPLAY_HEIGHT - 1);
		maxAxis = (DISPLAY_WIDTH - 1) * (DISPLAY_HEIGHT - 1);
	} else {
		minAxis = 0;
		maxAxis = 2 * (DISPLAY_WIDTH - 1) * (DISPLAY_HEIGHT - 1);
	}

	const int progress = (wave.frame * 100) / (WAVE_FRAME_COUNT - 1);
	const int start = minAxis - DIAGONAL_BAR_MARGIN;
	const int end = maxAxis + DIAGONAL_BAR_MARGIN;
	const int center = start + (end - start) * progress / 100;
	const int fade = 100 - (wave.frame * 25) / (WAVE_FRAME_COUNT - 1);

	for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
		for (int x = 0; x < DISPLAY_WIDTH; ++x) {
			int axis = 0;
			if (wave.diagonalDirection == DIAGONAL_LEFT_TOP_TO_RIGHT_BOTTOM) {
				axis = (DISPLAY_HEIGHT - 1) * x - (DISPLAY_WIDTH - 1) * y;
			} else {
				axis = (DISPLAY_HEIGHT - 1) * x + (DISPLAY_WIDTH - 1) * y;
			}
			int delta = axis - center;
			if (delta < 0) {
				delta = -delta;
			}

			Color color;
			int strength = 0;
			if (delta <= 30) {
				color = wave.core;
				strength = 100;
			} else if (delta <= 90) {
				color = wave.inner;
				strength = 82;
			} else if (delta <= 170) {
				color = wave.outer;
				strength = 52;
			} else if (delta <= 280) {
				color = wave.glow;
				strength = 28;
			}

			if (strength > 0) {
				setPixelAdd(rgbData, x, y, color, strength * fade, 10000);
			}
		}
	}
}

void LightWaveOverlay::advanceWaves() {
	for (std::vector<WaveState>::iterator it = mWaves.begin();
		 it != mWaves.end();) {
		++it->frame;
		if (it->frame >= WAVE_FRAME_COUNT) {
			it = mWaves.erase(it);
		} else {
			++it;
		}
	}
}

void LightWaveOverlay::originForDirection(Direction direction, int& originX2, int& originY2) const {
	switch (direction) {
	case DIRECTION_FROM_BOTTOM:
		originX2 = DISPLAY_WIDTH - 1;
		originY2 = (DISPLAY_HEIGHT - 1) * 2;
		break;
	case DIRECTION_FROM_LEFT:
		originX2 = 0;
		originY2 = DISPLAY_HEIGHT - 1;
		break;
	case DIRECTION_FROM_TOP:
		originX2 = DISPLAY_WIDTH - 1;
		originY2 = 0;
		break;
	case DIRECTION_FROM_RIGHT:
		originX2 = (DISPLAY_WIDTH - 1) * 2;
		originY2 = DISPLAY_HEIGHT - 1;
		break;
	default:
		originX2 = DISPLAY_WIDTH - 1;
		originY2 = 0;
		break;
	}
}

int LightWaveOverlay::maxRadiusSq4FromOrigin(int originX2, int originY2) const {
	const int corners[4][2] = {
		{0, 0},
		{(DISPLAY_WIDTH - 1) * 2, 0},
		{0, (DISPLAY_HEIGHT - 1) * 2},
		{(DISPLAY_WIDTH - 1) * 2, (DISPLAY_HEIGHT - 1) * 2}
	};
	int maxRadiusSq4 = 0;
	for (int i = 0; i < 4; ++i) {
		int dx2 = corners[i][0] - originX2;
		int dy2 = corners[i][1] - originY2;
		int distSq4 = dx2 * dx2 + dy2 * dy2;
		if (distSq4 > maxRadiusSq4) {
			maxRadiusSq4 = distSq4;
		}
	}
	return maxRadiusSq4 + 900;
}

void LightWaveOverlay::setPixelAdd(std::vector<uint8_t>& rgbData, int x, int y,
	const Color& color, int strength, int maxStrength) {
	if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) {
		return;
	}

	Color scaled = scaleColor(color, strength, maxStrength);
	int index = (y * DISPLAY_WIDTH + x) * 3;
	int r = rgbData[index + 0] + scaled.r;
	int g = rgbData[index + 1] + scaled.g;
	int b = rgbData[index + 2] + scaled.b;
	rgbData[index + 0] = r > 255 ? 255 : r;
	rgbData[index + 1] = g > 255 ? 255 : g;
	rgbData[index + 2] = b > 255 ? 255 : b;
}

Color LightWaveOverlay::scaleColor(const Color& color, int strength, int maxStrength) {
	if (strength < 0) {
		strength = 0;
	} else if (strength > maxStrength) {
		strength = maxStrength;
	}

	return Color(
		color.r * strength / maxStrength,
		color.g * strength / maxStrength,
		color.b * strength / maxStrength);
}

void LightWaveOverlay::sendFrame(const std::vector<uint8_t>& rgbData) {
	PageBase::sendRawLedData(rgbData);
}
