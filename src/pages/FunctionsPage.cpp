#include "pages/FunctionsPage.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/time.h>

#include "base/base.h"
#include "managers/MenuIndicator.h"
#include "managers/McuManager.h"
#include "net/NetManager.h"

#define WIFIMANAGER NETMANAGER->getWifiManager()

namespace {

static const Color COLOR_BLACK(0, 0, 0);
static const Color COLOR_WHITE(255, 255, 255);
static const Color COLOR_DIM(55, 55, 55);
static const Color COLOR_RED(255, 60, 50);
static const Color COLOR_RED_DARK(145, 12, 24);
static const Color COLOR_RED_DEEP(90, 0, 18);
static const Color COLOR_GREEN(60, 255, 120);
static const Color COLOR_GREEN_DARK(20, 130, 55);
static const Color COLOR_YELLOW(255, 210, 60);
static const Color COLOR_CYAN(60, 220, 255);
static const Color COLOR_MAGENTA(255, 80, 210);
static const Color COLOR_BLUE(55, 120, 255);
static const Color COLOR_SKY(90, 205, 255);
static const Color COLOR_ORANGE(255, 145, 35);
static const Color COLOR_PURPLE(160, 95, 255);

#define DISPLAY_WIDTH 52
#define DISPLAY_HEIGHT 16
#define POMODORO_SECONDS (25 * 60)
#define POMODORO_REST_SECONDS (5 * 60)
#define POMODORO_RESET_LONG_PRESS_MS 1500ULL
#define IP_SCROLL_SPEED_MS 70
#define IP_QUERY_INTERVAL_MS 2000
#define WEATHER_PAGE_INTERVAL_MS 3500
#define LARGE_DIGIT_SCALE 2
#define LARGE_DIGIT_WIDTH (3 * LARGE_DIGIT_SCALE)
#define LARGE_DIGIT_HEIGHT (5 * LARGE_DIGIT_SCALE)
#define LARGE_CHAR_GAP 2
#define DOT_WIDTH 1
#define MIC_BAR_COUNT DISPLAY_WIDTH
#define MIC_RAW_NOISE_GATE_PERCENT 4
#define MIC_IDLE_PERCENT 0
#define MIC_PALETTE_COUNT 5
#define MIC_BAND_COUNT 5
#define PET_ACTION_COUNT 6
#define PET_SPECIES_COUNT 3
#define PET_AUTO_DURATION_SCALE 2
#define PET_LEFT_LONG_PRESS_MS 800ULL
#define PET_RIGHT_LONG_PRESS_MS 800ULL
#define PET_MIDDLE_LONG_PRESS_MS 900ULL

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

static int clampInt(int value, int minValue, int maxValue) {
	if (value < minValue) {
		return minValue;
	}
	if (value > maxValue) {
		return maxValue;
	}
	return value;
}

static Color blendColor(const Color& from, const Color& to, int value,
		int maxValue) {
	if (maxValue <= 0) {
		return to;
	}
	value = clampInt(value, 0, maxValue);
	return Color(
		static_cast<Color::byte>(from.r + (to.r - from.r) * value / maxValue),
		static_cast<Color::byte>(from.g + (to.g - from.g) * value / maxValue),
		static_cast<Color::byte>(from.b + (to.b - from.b) * value / maxValue));
}

static Color scaleColor(const Color& color, int percent) {
	percent = clampInt(percent, 0, 100);
	return Color(
		static_cast<Color::byte>(color.r * percent / 100),
		static_cast<Color::byte>(color.g * percent / 100),
		static_cast<Color::byte>(color.b * percent / 100));
}

static int noiseAt(int seed, int maxValue) {
	if (maxValue <= 0) {
		return 0;
	}
	unsigned int value = static_cast<unsigned int>(seed) * 1103515245U + 12345U;
	value ^= value >> 16;
	value *= 2246822519U;
	value ^= value >> 13;
	return static_cast<int>(value % static_cast<unsigned int>(maxValue));
}

struct MicPalette {
	Color bottom;
	Color lower;
	Color upper;
	Color top;
};

static const MicPalette& micPalette(int paletteIndex) {
	static const MicPalette PALETTES[MIC_PALETTE_COUNT] = {
		{ Color(60, 255, 120), Color(60, 220, 255), Color(255, 210, 60), Color(255, 80, 210) },
		{ Color(0, 190, 255), Color(80, 255, 150), Color(255, 230, 40), Color(255, 70, 80) },
		{ Color(80, 255, 80), Color(80, 180, 255), Color(190, 90, 255), Color(255, 80, 150) },
		{ Color(255, 95, 70), Color(255, 220, 50), Color(80, 255, 170), Color(40, 190, 255) },
		{ Color(80, 210, 255), Color(120, 255, 100), Color(255, 160, 50), Color(255, 60, 200) }
	};
	return PALETTES[clampInt(paletteIndex, 0, MIC_PALETTE_COUNT - 1)];
}

static Color micGradientColor(int rowFromBottom, int paletteIndex) {
	const MicPalette& palette = micPalette(paletteIndex);
	const int pos = clampInt(rowFromBottom * 100 / (DISPLAY_HEIGHT - 1), 0, 100);
	if (pos < 38) {
		return blendColor(palette.bottom, palette.lower, pos, 38);
	}
	if (pos < 72) {
		return blendColor(palette.lower, palette.upper, pos - 38, 34);
	}
	return blendColor(palette.upper, palette.top, pos - 72, 28);
}

static int percentToRows(int percent) {
	if (percent <= 0) {
		return 1;
	}
	return clampInt(1 + percent * (DISPLAY_HEIGHT - 1) / 100, 1, DISPLAY_HEIGHT);
}

static int micBandForColumn(int column) {
	if (column < 8) {
		return 0;
	}
	if (column < 20) {
		return 1;
	}
	if (column < 33) {
		return 2;
	}
	if (column < 44) {
		return 3;
	}
	return 4;
}

static int micBandTargetPercent(int band, int percent, int punch, int frame) {
	static const int baseGain[MIC_BAND_COUNT] = { 116, 96, 84, 68, 58 };
	static const int punchGain[MIC_BAND_COUNT] = { 30, 52, 74, 100, 118 };
	static const int pulseSpeed[MIC_BAND_COUNT] = { 2, 4, 6, 9, 12 };
	static const int pulseDepth[MIC_BAND_COUNT] = { 8, 13, 18, 25, 32 };

	const int pulse = ((frame * pulseSpeed[band] + band * 37) % 101) - 50;
	int target = percent * baseGain[band] / 100 +
		punch * punchGain[band] / 100 +
		pulse * pulseDepth[band] * (percent + punch) / 10000;

	if (band >= 3 && punch > 5) {
		target += ((frame + band * 11) % 3) * punch / 2;
	}
	if (band == 0 && percent > 35) {
		target += (percent - 35) / 3;
	}
	return clampInt(target, 0, 100);
}

static int micColumnTarget(int column, int frame, const int* bandLevels) {
	const int band = micBandForColumn(column);
	if (bandLevels[band] <= 1 &&
			(band == 0 || bandLevels[band - 1] <= 1) &&
			(band == MIC_BAND_COUNT - 1 || bandLevels[band + 1] <= 1)) {
		return 1;
	}

	int target = bandLevels[band];
	if (band > 0) {
		target = target * 78 / 100 + bandLevels[band - 1] * 22 / 100;
	}
	if (band < MIC_BAND_COUNT - 1) {
		target = target * 82 / 100 + bandLevels[band + 1] * 18 / 100;
	}

	const int rippleA = ((frame * (band + 2) + column * 7) % 17) - 8;
	const int rippleB = ((frame * 3 + column * 13) % 11) - 5;
	const int texture = ((column * 31 + frame * (band + 5)) % 9) - 4;
	target += rippleA * (band + 2) / 5;
	target += rippleB * (band + 1) / 6;
	target += texture;

	if (band >= 3 && ((frame + column * 5) % 7) == 0) {
		target += 2 + band;
	}
	return clampInt(target, 1, DISPLAY_HEIGHT);
}

enum WeatherKind {
	WEATHER_CLEAR,
	WEATHER_CLOUD,
	WEATHER_FOG,
	WEATHER_RAIN,
	WEATHER_SNOW,
	WEATHER_THUNDER
};

static WeatherKind weatherKindForCode(int code) {
	if (code == 95 || code == 96 || code == 99) {
		return WEATHER_THUNDER;
	}
	if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
		return WEATHER_RAIN;
	}
	if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
		return WEATHER_SNOW;
	}
	if (code == 45 || code == 48) {
		return WEATHER_FOG;
	}
	if (code >= 1 && code <= 3) {
		return WEATHER_CLOUD;
	}
	return WEATHER_CLEAR;
}

static Color aqiColor(int aqi) {
	if (aqi <= 50) {
		return COLOR_GREEN;
	}
	if (aqi <= 100) {
		return COLOR_YELLOW;
	}
	if (aqi <= 150) {
		return COLOR_ORANGE;
	}
	return COLOR_RED;
}

} // namespace

FunctionsPage::FunctionsPage()
	: PageBase("FunctionsPage"),
	  mCurrentFunction(FUNC_POMODORO),
	  mPomodoroRunning(false),
	  mPomodoroResting(false),
	  mPomodoroRemainingSec(POMODORO_SECONDS),
	  mPomodoroLastTickMs(0),
	  mMiddlePressStartMs(0),
	  mMiddleLongResetDone(false),
	  mPetSpeciesIndex(0),
	  mPetAction(PET_AUTO),
	  mPetAnimation(petAnimationForAction(PET_AUTO)),
	  mPetFrameIndex(0),
	  mPetLastFrameMs(0),
	  mPetMiddlePressStartMs(0),
	  mPetMiddleLongSpeciesDone(false),
	  mPetLeftPressStartMs(0),
	  mPetLeftPressed(false),
	  mPetRightPressStartMs(0),
	  mPetRightPressed(false),
	  mPetLabelFrame(0),
	  mAmbienceScene(AMBIENCE_FIRE),
	  mAmbienceFrame(0),
	  mAmbienceSceneSelectMode(false),
	  mMetronomeRunning(false),
	  mMetronomeBpm(90),
	  mMetronomeFrame(0),
	  mMetronomeLastBeatMs(0),
	  mMetronomeBeatFlashMs(0),
	  mChargeFrame(0),
	  mTomatoFrame(0),
	  mMicPercent(0),
	  mMicFrame(0),
	  mMicPaletteIndex(0),
	  mMicHasTriggered(false),
	  mMicAutoReportEnabled(false),
	  mIpScrollStartMs(0),
	  mCachedIp("192.168.1.13"),
	  mLastIpQueryMs(0),
	  mWeatherFrame(0),
	  mWeatherPageStartMs(0) {
	for (int i = 0; i < MIC_BAR_COUNT; ++i) {
		mMicColumnLevels[i] = 1;
		mMicPeakLevels[i] = 1;
	}
	for (int i = 0; i < MIC_BAND_COUNT; ++i) {
		mMicBandLevels[i] = 1;
	}
}

FunctionsPage::~FunctionsPage() {
}

void FunctionsPage::onEnter() {
	LOGD_TRACE("FunctionsPage: onEnter");
	mPomodoroLastTickMs = nowMs();
	mMetronomeLastBeatMs = mPomodoroLastTickMs;
	if (mCurrentFunction == FUNC_WEATHER) {
		mWeatherPageStartMs = mPomodoroLastTickMs;
	}
	updateMicReportState();
	updateWeatherState(false);
	MenuIndicator::getInstance().show(FUNC_COUNT, mCurrentFunction);
	draw();
}

void FunctionsPage::onExit() {
	LOGD_TRACE("FunctionsPage: onExit");
	mMiddlePressStartMs = 0;
	mMiddleLongResetDone = false;
	mPetMiddlePressStartMs = 0;
	mPetMiddleLongSpeciesDone = false;
	mPetLeftPressStartMs = 0;
	mPetLeftPressed = false;
	mPetRightPressStartMs = 0;
	mPetRightPressed = false;
	mAmbienceSceneSelectMode = false;
	if (mMicAutoReportEnabled) {
		McuManager::getInstance().setAutoMicReport(false);
		mMicAutoReportEnabled = false;
	}
	mWeatherService.stop();
}

bool FunctionsPage::onKeyEvent(int keyCode, int keyStatus) {
	if ((keyCode == E_KEYCODE_CLOCKWISE || keyCode == E_KEYCODE_ANTI_CLOCKWISE) &&
			keyStatus == 0) {
		if (mCurrentFunction == FUNC_AMBIENCE && mAmbienceSceneSelectMode) {
			switchAmbienceScene(keyCode == E_KEYCODE_CLOCKWISE ? 1 : -1);
			return false;
		}
		if (mCurrentFunction == FUNC_METRONOME) {
			const int delta = keyCode == E_KEYCODE_CLOCKWISE ? 5 : -5;
			mMetronomeBpm = clampInt(mMetronomeBpm + delta, 40, 220);
			mMetronomeLastBeatMs = nowMs();
			mMetronomeBeatFlashMs = mMetronomeLastBeatMs;
			draw();
			return false;
		}
		switchFunction(keyCode == E_KEYCODE_CLOCKWISE ? 1 : -1);
		return false;
	}

	if (keyCode == E_KEYCODE_MIDDLE_BUTTON &&
			mCurrentFunction == FUNC_POMODORO) {
		uint64_t currentMs = nowMs();
		if (keyStatus == 1) {
			mMiddlePressStartMs = currentMs;
			mMiddleLongResetDone = false;
			return false;
		}
		if (keyStatus == 0 && mMiddlePressStartMs > 0) {
			if (!mMiddleLongResetDone &&
					currentMs - mMiddlePressStartMs >= POMODORO_RESET_LONG_PRESS_MS) {
				resetPomodoro();
				mMiddleLongResetDone = true;
			}
			if (!mMiddleLongResetDone) {
				togglePomodoro();
			}
			mMiddlePressStartMs = 0;
			mMiddleLongResetDone = false;
			return false;
		}
	}

	if (keyCode == E_KEYCODE_MIDDLE_BUTTON &&
			mCurrentFunction == FUNC_WEATHER) {
		if (keyStatus == 1) {
			updateWeatherState(true);
			draw();
			return false;
		}
		return false;
	}

	if (mCurrentFunction == FUNC_PET) {
		const uint64_t currentMs = nowMs();
		if (keyCode == E_KEYCODE_MIDDLE_BUTTON) {
			if (keyStatus == 1) {
				mPetMiddlePressStartMs = currentMs;
				mPetMiddleLongSpeciesDone = false;
			} else if (keyStatus == 0 && mPetMiddlePressStartMs > 0) {
				if (!mPetMiddleLongSpeciesDone &&
						currentMs - mPetMiddlePressStartMs >= PET_MIDDLE_LONG_PRESS_MS) {
					switchPetSpecies(1);
				} else if (!mPetMiddleLongSpeciesDone) {
					selectPetAction(PET_HAPPY);
				}
				mPetMiddlePressStartMs = 0;
				mPetMiddleLongSpeciesDone = false;
			}
			return false;
		}

		if (keyCode == E_KEYCODE_LEFT_BUTTON) {
			if (keyStatus == 1) {
				mPetLeftPressStartMs = currentMs;
				mPetLeftPressed = true;
			} else if (keyStatus == 0 && mPetLeftPressed) {
				const uint64_t heldMs = currentMs - mPetLeftPressStartMs;
				selectPetAction(heldMs >= PET_LEFT_LONG_PRESS_MS ?
					PET_AUTO : PET_WALK);
				mPetLeftPressStartMs = 0;
				mPetLeftPressed = false;
			}
			return false;
		}

		if (keyCode == E_KEYCODE_RIGHT_BUTTON) {
			if (keyStatus == 1) {
				mPetRightPressStartMs = currentMs;
				mPetRightPressed = true;
			} else if (keyStatus == 0 && mPetRightPressed) {
				const uint64_t heldMs = currentMs - mPetRightPressStartMs;
				selectPetAction(heldMs >= PET_RIGHT_LONG_PRESS_MS ?
					PET_SLEEP : PET_EAT);
				mPetRightPressStartMs = 0;
				mPetRightPressed = false;
			}
			return false;
		}

		if (keyStatus == 1) {
			if (keyCode == E_KEYCODE_KNOB_BUTTON) {
				selectPetAction(PET_AUTO);
				return false;
			}
		}
	}

	if (keyCode == E_KEYCODE_MIDDLE_BUTTON &&
			mCurrentFunction == FUNC_AMBIENCE) {
		if (keyStatus == 1) {
			mAmbienceSceneSelectMode = !mAmbienceSceneSelectMode;
			draw();
		}
		return false;
	}

	if (keyCode == E_KEYCODE_MIDDLE_BUTTON &&
			mCurrentFunction == FUNC_METRONOME) {
		if (keyStatus == 1) {
			mMetronomeRunning = !mMetronomeRunning;
			mMetronomeLastBeatMs = nowMs();
			mMetronomeBeatFlashMs = mMetronomeLastBeatMs;
			draw();
		}
		return false;
	}

	if (keyStatus != 1) {
		return true;
	}

	switch (keyCode) {
	case E_KEYCODE_LEFT_BUTTON:
		switchFunction(-1);
		return false;
	case E_KEYCODE_RIGHT_BUTTON:
		switchFunction(1);
		return false;
	default:
		return true;
	}
}

bool FunctionsPage::needsFastRefresh() const {
	return mCurrentFunction == FUNC_IP ||
		mCurrentFunction == FUNC_PET ||
		mCurrentFunction == FUNC_AMBIENCE ||
		mCurrentFunction == FUNC_BATTERY ||
		mCurrentFunction == FUNC_MIC_LIGHT ||
		mCurrentFunction == FUNC_WEATHER ||
		(mCurrentFunction == FUNC_METRONOME && mMetronomeRunning) ||
		(mCurrentFunction == FUNC_POMODORO &&
			(mPomodoroRunning || mMiddlePressStartMs > 0));
}

void FunctionsPage::draw() {
	uint64_t currentMs = nowMs();
	updatePomodoro(currentMs);
	updateMiddleLongPress(currentMs);
	updateMetronome(currentMs);
	updatePetAnimation(currentMs);
	Surface surface(DISPLAY_WIDTH, DISPLAY_HEIGHT, COLOR_BLACK);

	if (mCurrentFunction == FUNC_BATTERY) {
		drawBattery(surface);
	} else if (mCurrentFunction == FUNC_PET) {
		drawPet(surface);
	} else if (mCurrentFunction == FUNC_AMBIENCE) {
		drawAmbience(surface);
	} else if (mCurrentFunction == FUNC_METRONOME) {
		drawMetronome(surface, currentMs);
	} else if (mCurrentFunction == FUNC_IP) {
		drawIpAddress(surface, currentMs);
	} else if (mCurrentFunction == FUNC_MIC_LIGHT) {
		drawMicLight(surface);
	} else if (mCurrentFunction == FUNC_WEATHER) {
		drawWeather(surface, currentMs);
	} else {
		drawPomodoro(surface);
	}

	std::vector<uint8_t> data;
	surface.extractRGB(data);
	MenuIndicator::getInstance().show(FUNC_COUNT, mCurrentFunction);
	sendLedData(data);
}

uint64_t FunctionsPage::nowMs() const {
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	return static_cast<uint64_t>(tv.tv_sec) * 1000ULL +
		static_cast<uint64_t>(tv.tv_usec) / 1000ULL;
}

void FunctionsPage::switchFunction(int delta) {
	mAmbienceSceneSelectMode = false;
	mPetMiddlePressStartMs = 0;
	mPetMiddleLongSpeciesDone = false;
	mPetLeftPressStartMs = 0;
	mPetLeftPressed = false;
	mPetRightPressStartMs = 0;
	mPetRightPressed = false;
	mCurrentFunction += delta;
	while (mCurrentFunction < 0) {
		mCurrentFunction += FUNC_COUNT;
	}
	while (mCurrentFunction >= FUNC_COUNT) {
		mCurrentFunction -= FUNC_COUNT;
	}
	if (mCurrentFunction == FUNC_IP) {
		mIpScrollStartMs = nowMs();
	}
	if (mCurrentFunction == FUNC_WEATHER) {
		mWeatherPageStartMs = nowMs();
		updateWeatherState(false);
	}
	if (mCurrentFunction == FUNC_BATTERY) {
		mChargeFrame = 0;
	}
	updateMicReportState();
	MenuIndicator::getInstance().show(FUNC_COUNT, mCurrentFunction);
	draw();
}

void FunctionsPage::togglePomodoro() {
	mPomodoroRunning = !mPomodoroRunning;
	mPomodoroLastTickMs = nowMs();
	draw();
}

void FunctionsPage::resetPomodoro() {
	mPomodoroRunning = false;
	mPomodoroResting = false;
	mPomodoroRemainingSec = POMODORO_SECONDS;
	mPomodoroLastTickMs = nowMs();
	draw();
}

void FunctionsPage::advancePomodoroPhase() {
	mPomodoroResting = !mPomodoroResting;
	mPomodoroRemainingSec = mPomodoroResting ?
		POMODORO_REST_SECONDS : POMODORO_SECONDS;
}

void FunctionsPage::updatePomodoro(uint64_t currentMs) {
	if (!mPomodoroRunning) {
		mPomodoroLastTickMs = currentMs;
		return;
	}

	if (currentMs <= mPomodoroLastTickMs) {
		return;
	}

	int elapsedSec = static_cast<int>((currentMs - mPomodoroLastTickMs) / 1000);
	if (elapsedSec <= 0) {
		return;
	}

	mPomodoroRemainingSec -= elapsedSec;
	mPomodoroLastTickMs += static_cast<uint64_t>(elapsedSec) * 1000ULL;
	if (mPomodoroRemainingSec <= 0) {
		advancePomodoroPhase();
	}
}

void FunctionsPage::updateMiddleLongPress(uint64_t currentMs) {
	if (mCurrentFunction != FUNC_POMODORO ||
			mMiddlePressStartMs <= 0 ||
			mMiddleLongResetDone) {
		return;
	}
	if (currentMs - mMiddlePressStartMs >= POMODORO_RESET_LONG_PRESS_MS) {
		mPomodoroRunning = false;
		mPomodoroResting = false;
		mPomodoroRemainingSec = POMODORO_SECONDS;
		mPomodoroLastTickMs = currentMs;
		mMiddleLongResetDone = true;
	}
}

void FunctionsPage::updateMetronome(uint64_t currentMs) {
	if (!mMetronomeRunning) {
		mMetronomeLastBeatMs = currentMs;
		return;
	}

	const int intervalMs = 60000 / clampInt(mMetronomeBpm, 40, 220);
	if (mMetronomeLastBeatMs == 0) {
		mMetronomeLastBeatMs = currentMs;
		mMetronomeBeatFlashMs = currentMs;
		return;
	}
	while (currentMs >= mMetronomeLastBeatMs + static_cast<uint64_t>(intervalMs)) {
		mMetronomeLastBeatMs += static_cast<uint64_t>(intervalMs);
		mMetronomeBeatFlashMs = mMetronomeLastBeatMs;
	}
	mMetronomeFrame++;
}

void FunctionsPage::drawPomodoro(Surface& surface) {
	drawTomatoIcon(surface, 1, 1);
	const int minutes = mPomodoroRemainingSec / 60;
	const int seconds = mPomodoroRemainingSec % 60;
	const Color textColor = mPomodoroResting ? COLOR_YELLOW : COLOR_WHITE;
	const Color colonColor = mPomodoroRunning ? COLOR_WHITE : COLOR_YELLOW;
	drawTwoDigitNumber(surface, 15, 13, minutes, textColor);
	Painter::getInstance().drawRect(surface, 31, 5, 2, 2, colonColor, true);
	Painter::getInstance().drawRect(surface, 31, 10, 2, 2, colonColor, true);
	drawTwoDigitNumber(surface, 35, 13, seconds, textColor);
	if (mPomodoroRunning) {
		mTomatoFrame++;
	}

	const int pulse = mPomodoroRunning ? (mTomatoFrame % 12) : 0;
	const Color ritualColor = mPomodoroResting ? COLOR_CYAN : COLOR_GREEN;
	for (int i = 0; i < 9; ++i) {
		const int x = 18 + i * 3;
		const int bright = (i + pulse / 2) % 4 == 0 ? 100 : 25;
		Painter::getInstance().drawPixel(surface, x, 15,
			scaleColor(ritualColor, bright));
	}
}

void FunctionsPage::switchAmbienceScene(int delta) {
	mAmbienceScene += delta;
	while (mAmbienceScene < 0) {
		mAmbienceScene += AMBIENCE_COUNT;
	}
	while (mAmbienceScene >= AMBIENCE_COUNT) {
		mAmbienceScene -= AMBIENCE_COUNT;
	}
	mAmbienceFrame = 0;
	draw();
}

void FunctionsPage::drawAmbience(Surface& surface) {
	switch (mAmbienceScene) {
	case AMBIENCE_RAIN:
		drawRainAmbience(surface);
		break;
	case AMBIENCE_SNOW:
		drawSnowAmbience(surface);
		break;
	case AMBIENCE_STAR:
		drawStarAmbience(surface);
		break;
	case AMBIENCE_WAVE:
		drawWaveAmbience(surface);
		break;
	case AMBIENCE_AURORA:
		drawAuroraAmbience(surface);
		break;
	case AMBIENCE_BREATH:
		drawBreathAmbience(surface);
		break;
	case AMBIENCE_HEART:
		drawHeartAmbience(surface);
		break;
	case AMBIENCE_EQUALIZER:
		drawEqualizerAmbience(surface);
		break;
	case AMBIENCE_FIRE:
	default:
		drawFireAmbience(surface);
		break;
	}

	if (mAmbienceSceneSelectMode || mAmbienceFrame < 24) {
		drawAmbienceSceneName(surface);
	}
	mAmbienceFrame++;
}

void FunctionsPage::drawFireAmbience(Surface& surface) {
	for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
		for (int x = 0; x < DISPLAY_WIDTH; ++x) {
			const int heat = DISPLAY_HEIGHT - y;
			const int flicker = noiseAt(x * 31 + y * 17 + mAmbienceFrame * 9, 9);
			const int flame = heat * 9 + flicker + ((x + mAmbienceFrame) % 7);
			if (flame > 52) {
				Painter::getInstance().drawPixel(surface, x, y, COLOR_YELLOW);
			} else if (flame > 38) {
				Painter::getInstance().drawPixel(surface, x, y, COLOR_ORANGE);
			} else if (flame > 24) {
				Painter::getInstance().drawPixel(surface, x, y, COLOR_RED);
			} else if (y > 11) {
				Painter::getInstance().drawPixel(surface, x, y, COLOR_RED_DEEP);
			}
		}
	}
}

void FunctionsPage::drawRainAmbience(Surface& surface) {
	for (int i = 0; i < 38; ++i) {
		const int x = noiseAt(i * 29, DISPLAY_WIDTH);
		const int y = (noiseAt(i * 43, DISPLAY_HEIGHT) + mAmbienceFrame + i * 3) %
			DISPLAY_HEIGHT;
		const Color color = i % 3 == 0 ? COLOR_WHITE : (i % 2 ? COLOR_CYAN : COLOR_BLUE);
		Painter::getInstance().drawPixel(surface, x, y, color);
		Painter::getInstance().drawPixel(surface, x - 1, y - 1, scaleColor(color, 45));
	}
	for (int x = 0; x < DISPLAY_WIDTH; ++x) {
		const int wave = 14 + ((x + mAmbienceFrame / 2) % 4 == 0 ? 1 : 0);
		Painter::getInstance().drawPixel(surface, x, wave, scaleColor(COLOR_BLUE, 35));
	}
}

void FunctionsPage::drawSnowAmbience(Surface& surface) {
	for (int i = 0; i < 24; ++i) {
		const int drift = (mAmbienceFrame / 5 + i * 3) % 7 - 3;
		const int x = (noiseAt(i * 37, DISPLAY_WIDTH) + drift + DISPLAY_WIDTH) %
			DISPLAY_WIDTH;
		const int y = (noiseAt(i * 53, DISPLAY_HEIGHT) + mAmbienceFrame / 2 + i) %
			DISPLAY_HEIGHT;
		const Color color = i % 3 == 0 ? COLOR_CYAN : COLOR_WHITE;
		Painter::getInstance().drawPixel(surface, x, y, color);
		if (i % 5 == 0) {
			Painter::getInstance().drawPixel(surface, x + 1, y, scaleColor(color, 55));
			Painter::getInstance().drawPixel(surface, x, y + 1, scaleColor(color, 55));
		}
	}
}

void FunctionsPage::drawStarAmbience(Surface& surface) {
	for (int i = 0; i < 32; ++i) {
		const int x = noiseAt(i * 61, DISPLAY_WIDTH);
		const int y = noiseAt(i * 47, DISPLAY_HEIGHT);
		const int twinkle = noiseAt(i * 13 + mAmbienceFrame / 3, 100);
		Color color = twinkle > 78 ? COLOR_WHITE :
			(twinkle > 55 ? COLOR_YELLOW : scaleColor(COLOR_BLUE, 22));
		Painter::getInstance().drawPixel(surface, x, y, color);
		if (twinkle > 90) {
			Painter::getInstance().drawPixel(surface, x - 1, y, scaleColor(color, 50));
			Painter::getInstance().drawPixel(surface, x + 1, y, scaleColor(color, 50));
		}
	}
}

void FunctionsPage::drawWaveAmbience(Surface& surface) {
	for (int x = 0; x < DISPLAY_WIDTH; ++x) {
		const int waveA = ((x * 3 + mAmbienceFrame * 2) % 18) - 9;
		const int waveB = ((x * 7 + mAmbienceFrame * 5) % 22) - 11;
		const int crest = clampInt(9 + waveA / 3 + waveB / 6, 4, 12);
		const int foam = noiseAt(x * 41 + mAmbienceFrame * 11, 100);

		for (int y = crest; y < DISPLAY_HEIGHT; ++y) {
			const int depth = y - crest;
			Color color = depth == 0 ? COLOR_CYAN :
				(depth < 3 ? COLOR_BLUE : scaleColor(COLOR_BLUE, 42));
			if (depth > 5) {
				color = scaleColor(COLOR_BLUE, 28);
			}
			if ((foam + depth * 13) % 31 < 6 && depth <= 2) {
				color = COLOR_WHITE;
			}
			Painter::getInstance().drawPixel(surface, x, y, color);
		}

		if ((x + mAmbienceFrame) % 5 == 0) {
			Painter::getInstance().drawPixel(surface, x, crest - 1, COLOR_WHITE);
		}
		if (((x * 3 + mAmbienceFrame) % 11) == 0) {
			Painter::getInstance().drawPixel(surface, x, crest, COLOR_WHITE);
		}
		if (((x + mAmbienceFrame / 2) % 7) < 2) {
			Painter::getInstance().drawPixel(surface, x, crest + 1, COLOR_CYAN);
		}
	}

	for (int x = 0; x < DISPLAY_WIDTH; ++x) {
		const int rippleY = 6 + ((x * 4 + mAmbienceFrame * 3) % 16) / 4 -
			((x * 9 + mAmbienceFrame * 2) % 10) / 5;
		if (rippleY >= 4 && rippleY < DISPLAY_HEIGHT) {
			Painter::getInstance().drawPixel(surface, x, rippleY,
				scaleColor(COLOR_CYAN, 55));
		}
	}
}

void FunctionsPage::drawAuroraAmbience(Surface& surface) {
	for (int x = 0; x < DISPLAY_WIDTH; ++x) {
		const int ribbon = 3 + ((x * 3 + mAmbienceFrame) % 17) / 3;
		Color color = blendColor(COLOR_GREEN, COLOR_MAGENTA,
			(x * 100 / DISPLAY_WIDTH + mAmbienceFrame * 3) % 100, 100);
		for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
			const int distance = y > ribbon ? y - ribbon : ribbon - y;
			if (distance < 5) {
				Painter::getInstance().drawPixel(surface, x, y,
					scaleColor(color, 85 - distance * 16));
			}
		}
	}
}

void FunctionsPage::drawBreathAmbience(Surface& surface) {
	for (int stream = 0; stream < 36; ++stream) {
		const int x = (stream * 5 + noiseAt(stream * 97, 5)) % DISPLAY_WIDTH;
		const int speed = 1 + noiseAt(stream * 53, 2);
		const int tailLength = 5 + noiseAt(stream * 71, 7);
		const int period = DISPLAY_HEIGHT + tailLength + 5;
		const int headY = (mAmbienceFrame / speed +
			noiseAt(stream * 131, period)) % period;

		for (int tail = 0; tail < tailLength; ++tail) {
			const int y = headY - tail;
			if (y < 0 || y >= DISPLAY_HEIGHT) {
				continue;
			}

			Color rainColor = tail == 0 ? Color(185, 255, 205) :
				(tail == 1 ? COLOR_GREEN : scaleColor(COLOR_GREEN, 92 - tail * 7));
			if (tail > 2 && ((stream + tail + mAmbienceFrame) % 5) == 0) {
				rainColor = scaleColor(rainColor, 45);
			}

			Painter::getInstance().drawPixel(surface, x, y, rainColor);
			if (tail > 0 && ((stream * 7 + tail + mAmbienceFrame / 2) % 6) == 0) {
				Painter::getInstance().drawPixel(surface, x + 1, y,
					scaleColor(rainColor, 45));
			}
			if (tail > 2 && ((stream * 11 + tail * 3 + mAmbienceFrame) % 13) == 0) {
				Painter::getInstance().drawPixel(surface, x - 1, y,
					scaleColor(COLOR_GREEN_DARK, 55));
			}
		}
	}
}

void FunctionsPage::drawHeartAmbience(Surface& surface) {
	static const int8_t points[][2] = {
		{-4,-2},{-3,-3},{-2,-3},{-1,-2},{0,-1},{1,-2},{2,-3},{3,-3},{4,-2},
		{-5,-1},{-5,0},{-4,1},{-3,2},{-2,3},{-1,4},{0,5},{1,4},{2,3},
		{3,2},{4,1},{5,0},{5,-1}
	};
	const int phase = mAmbienceFrame % 36;
	const int beat = phase < 5 || (phase >= 11 && phase < 15) ? 2 : 0;
	const Color color = phase < 15 ? COLOR_RED : scaleColor(COLOR_RED, 55);
	for (size_t i = 0; i < sizeof(points) / sizeof(points[0]); ++i) {
		const int x = 26 + points[i][0] * (beat + 3) / 3;
		const int y = 7 + points[i][1] * (beat + 3) / 3;
		Painter::getInstance().drawPixel(surface, x, y, color);
	}
	Painter::getInstance().drawLine(surface, 0, 8, 10, 8, scaleColor(COLOR_RED, 55));
	Painter::getInstance().drawLine(surface, 42, 8, 51, 8, scaleColor(COLOR_RED, 55));
}

void FunctionsPage::drawEqualizerAmbience(Surface& surface) {
	for (int x = 0; x < DISPLAY_WIDTH; ++x) {
		const int band = micBandForColumn(x);
		const int waveA = noiseAt(band * 41 + mAmbienceFrame * (band + 3), 9);
		const int waveB = noiseAt(x * 17 + mAmbienceFrame * 5, 7);
		const int sweep = (mAmbienceFrame + x * (band + 1)) % 24;
		int height = 2 + waveA + waveB / 2;
		if (sweep < 7) {
			height += 5 - sweep / 2;
		}
		height = clampInt(height, 1, DISPLAY_HEIGHT);

		for (int y = DISPLAY_HEIGHT - height; y < DISPLAY_HEIGHT; ++y) {
			const int rowFromBottom = DISPLAY_HEIGHT - 1 - y;
			const Color color = micGradientColor(
				rowFromBottom, (mAmbienceFrame / 36) % MIC_PALETTE_COUNT);
			Painter::getInstance().drawPixel(surface, x, y, color);
		}

		const int peak = clampInt(DISPLAY_HEIGHT - height - 1 +
			noiseAt(x * 19 + mAmbienceFrame / 2, 3), 0, DISPLAY_HEIGHT - 1);
		if ((x + mAmbienceFrame) % 3 != 0) {
			Painter::getInstance().drawPixel(surface, x, peak, COLOR_WHITE);
		}
	}
}

void FunctionsPage::drawAmbienceSceneName(Surface& surface) {
	static const char* NAMES[AMBIENCE_COUNT] = {
		"FIRE", "RAIN", "SNOW", "STAR", "WATER", "AUR", "BREAT", "HEART", "EQUAL"
	};
	Painter::getInstance().drawRect(surface, 10, 4, 32, 8,
		mAmbienceSceneSelectMode ? Color(8, 16, 22) : COLOR_BLACK, true);
	drawCenteredText(surface, NAMES[mAmbienceScene], 10,
		mAmbienceSceneSelectMode ? COLOR_YELLOW : COLOR_WHITE);
}

void FunctionsPage::drawMetronome(Surface& surface, uint64_t currentMs) {
	const int intervalMs = 60000 / clampInt(mMetronomeBpm, 40, 220);
	const int elapsed = mMetronomeRunning ?
		static_cast<int>((currentMs - mMetronomeLastBeatMs) % intervalMs) : 0;
	const int phase = mMetronomeRunning ? elapsed * 100 / intervalMs : 50;
	const bool flash = currentMs - mMetronomeBeatFlashMs < 110ULL;
	const Color beatColor = flash ? COLOR_YELLOW : COLOR_GREEN;

	Painter::getInstance().drawLine(surface, 5, 14, 23, 14, COLOR_DIM);
	Painter::getInstance().drawLine(surface, 14, 14, 14, 3, COLOR_DIM);
	drawMetronomeNeedle(surface, phase, beatColor);
	Painter::getInstance().drawCircle(surface, 14, 14, 2, COLOR_WHITE, true);

	char bpm[4] = {0};
	snprintf(bpm, sizeof(bpm), "%d", clampInt(mMetronomeBpm, 40, 220));
	Painter::getInstance().drawText(surface, 28, 8, bpm, COLOR_WHITE, 0);
	Painter::getInstance().drawText(surface, 28, 14,
		mMetronomeRunning ? "RUN" : "BPM", mMetronomeRunning ? COLOR_GREEN : COLOR_CYAN, 0);
	if (flash) {
		Painter::getInstance().drawRect(surface, 48, 1, 3, 14, COLOR_YELLOW, true);
	}
}

void FunctionsPage::drawMetronomeNeedle(Surface& surface, int phase,
		const Color& color) {
	const int offset = phase < 50 ? -8 + phase * 16 / 50 :
		8 - (phase - 50) * 16 / 50;
	const int tipX = 14 + offset;
	const int tipY = 3 + (offset < 0 ? -offset : offset) / 5;
	Painter::getInstance().drawLine(surface, 14, 14, tipX, tipY, color);
	Painter::getInstance().drawPixel(surface, tipX, tipY, COLOR_WHITE);
}

void FunctionsPage::drawTomatoIcon(Surface& surface, int x, int y) {
	static const int8_t shine[][2] = {
		{3, 3}, {5, 2}, {8, 3}, {9, 6},
		{7, 9}, {4, 9}, {2, 7}, {2, 4}
	};
	const int frame = mTomatoFrame % 8;

	Painter::getInstance().drawCircle(surface, x + 6, y + 7, 5, COLOR_RED_DEEP, true);
	Painter::getInstance().drawCircle(surface, x + 5, y + 5, 4, COLOR_RED_DARK, true);
	Painter::getInstance().drawCircle(surface, x + 8, y + 5, 4, COLOR_RED_DARK, true);
	Painter::getInstance().drawCircle(surface, x + 4, y + 8, 4, COLOR_RED_DARK, true);
	Painter::getInstance().drawCircle(surface, x + 8, y + 8, 4, COLOR_RED_DARK, true);
	Painter::getInstance().drawCircle(surface, x + 6, y + 6, 5, COLOR_RED, true);

	Painter::getInstance().drawLine(surface, x + 6, y + 6, x + 3, y + 3, COLOR_RED_DARK);
	Painter::getInstance().drawLine(surface, x + 6, y + 6, x + 9, y + 3, COLOR_RED_DARK);
	Painter::getInstance().drawLine(surface, x + 6, y + 6, x + 2, y + 8, COLOR_RED_DARK);
	Painter::getInstance().drawLine(surface, x + 6, y + 6, x + 9, y + 9, COLOR_RED_DARK);

	Painter::getInstance().drawLine(surface, x + 6, y + 6, x + 6, y + 2, COLOR_GREEN);
	Painter::getInstance().drawLine(surface, x + 6, y + 6, x + 3, y + 4, COLOR_GREEN);
	Painter::getInstance().drawLine(surface, x + 6, y + 6, x + 9, y + 4, COLOR_GREEN);
	Painter::getInstance().drawLine(surface, x + 6, y + 6, x + 4, y + 8, COLOR_GREEN_DARK);
	Painter::getInstance().drawLine(surface, x + 6, y + 6, x + 8, y + 8, COLOR_GREEN_DARK);
	Painter::getInstance().drawPixel(surface, x + 6, y + 6, COLOR_YELLOW);

	Painter::getInstance().drawPixel(
		surface,
		x + shine[frame][0],
		y + shine[frame][1],
		COLOR_WHITE);
	if (mPomodoroRunning) {
		Painter::getInstance().drawPixel(
			surface,
			x + shine[(frame + 7) % 8][0],
			y + shine[(frame + 7) % 8][1],
			COLOR_YELLOW);
	}
}

void FunctionsPage::switchPetSpecies(int delta) {
	mPetSpeciesIndex += delta;
	while (mPetSpeciesIndex < 0) {
		mPetSpeciesIndex += PET_SPECIES_COUNT;
	}
	while (mPetSpeciesIndex >= PET_SPECIES_COUNT) {
		mPetSpeciesIndex -= PET_SPECIES_COUNT;
	}
	mPetAnimation = petAnimationForAction(mPetAction);
	mPetFrameIndex = 0;
	mPetLastFrameMs = nowMs();
	mPetLabelFrame = 0;
	draw();
}

void FunctionsPage::selectPetAction(int action) {
	mPetAction = clampInt(action, PET_AUTO, PET_COUNT - 1);
	mPetAnimation = petAnimationForAction(mPetAction);
	mPetFrameIndex = 0;
	mPetLastFrameMs = nowMs();
	mPetLabelFrame = 0;
	draw();
}

const PetAnimationAssets::PetAnimationSet& FunctionsPage::currentPetSet() const {
	switch (mPetSpeciesIndex) {
	case 1:
		return PetAnimationAssets::DOG_SET;
	case 2:
		return PetAnimationAssets::RABBIT_SET;
	case 0:
	default:
		return PetAnimationAssets::CAT_SET;
	}
}

const PetAnimationAssets::Animation* FunctionsPage::petAnimationForAction(
		int action) const {
	const PetAnimationAssets::PetAnimationSet& petSet = currentPetSet();
	int index = clampInt(action, PET_AUTO, PET_COUNT - 1);
	if (index >= PET_ACTION_COUNT || index >= petSet.animationCount) {
		index = PET_AUTO;
	}
	return petSet.animations[index];
}

void FunctionsPage::updatePetAnimation(uint64_t currentMs) {
	if (mCurrentFunction != FUNC_PET) {
		return;
	}

	if (mPetMiddlePressStartMs > 0 &&
			!mPetMiddleLongSpeciesDone &&
			currentMs - mPetMiddlePressStartMs >= PET_MIDDLE_LONG_PRESS_MS) {
		mPetMiddleLongSpeciesDone = true;
		switchPetSpecies(1);
	}

	if (mPetAnimation == nullptr || mPetAnimation->frameCount == 0) {
		return;
	}
	if (mPetFrameIndex < 0 || mPetFrameIndex >= mPetAnimation->frameCount) {
		mPetFrameIndex = 0;
	}

	if (mPetLastFrameMs == 0) {
		mPetLastFrameMs = currentMs;
		return;
	}
	for (int guard = 0; guard < 12; ++guard) {
		const PetAnimationAssets::Frame& frame =
			mPetAnimation->frames[mPetFrameIndex];
		const uint64_t durationMs = frame.durationMs > 0 ? frame.durationMs : 200;
		const uint64_t frameDurationMs = mPetAction == PET_AUTO ?
			durationMs * PET_AUTO_DURATION_SCALE : durationMs;
		if (currentMs < mPetLastFrameMs + frameDurationMs) {
			break;
		}
		mPetLastFrameMs += frameDurationMs;
		mPetFrameIndex = (mPetFrameIndex + 1) % mPetAnimation->frameCount;
	}
	if (mPetLabelFrame < 40) {
		mPetLabelFrame++;
	}
}

void FunctionsPage::drawPet(Surface& surface) {
	if (mPetAnimation != nullptr && mPetAnimation->frameCount > 0) {
		const int frameIndex = clampInt(mPetFrameIndex, 0,
			mPetAnimation->frameCount - 1);
		drawPetFrame(surface, mPetAnimation->frames[frameIndex]);
	}
	if (mPetLabelFrame < 18) {
		drawPetLabel(surface);
	}
}

void FunctionsPage::drawPetFrame(Surface& surface,
		const PetAnimationAssets::Frame& frame) {
	for (uint16_t i = 0; i < frame.pixelCount; ++i) {
		const PetAnimationAssets::Pixel& pixel = frame.pixels[i];
		surface.setPixel(pixel.x, pixel.y, Color(pixel.r, pixel.g, pixel.b));
	}
}

void FunctionsPage::drawPetLabel(Surface& surface) {
	static const char* ACTIONS[PET_COUNT] = {
		"AUTO", "IDLE", "WALK", "HAPPY", "EAT", "SLEEP"
	};
	const char* petName = currentPetSet().name;
	const char* actionName = ACTIONS[clampInt(mPetAction, PET_AUTO, PET_COUNT - 1)];
	Painter::getInstance().drawRect(surface, 0, 0, 25, 8, COLOR_BLACK, true);
	Painter::getInstance().drawText(surface, 1, 6, petName, COLOR_YELLOW, 0);
	Painter::getInstance().drawRect(surface, 28, 8, 24, 8, COLOR_BLACK, true);
	Painter::getInstance().drawText(surface, 29, 14, actionName, COLOR_WHITE, 0);
}

void FunctionsPage::drawBattery(Surface& surface) {
	drawHeader(surface, "BATT");
	std::pair<int, int> battery = McuManager::getInstance().queryBatteryPower();
	int percent = clampInt(battery.first, 0, 100);
	bool charging = battery.second != 0;
	drawBatteryIcon(surface, 8, 6, percent, charging);

	char text[5] = {0};
	snprintf(text, sizeof(text), "%d", percent);
	Painter::getInstance().drawText(surface, 36, 13, text, COLOR_WHITE, 0);
	if (charging) {
		int x = 35 + (mChargeFrame % 4);
		Painter::getInstance().drawPixel(surface, x, 5, COLOR_YELLOW);
		Painter::getInstance().drawPixel(surface, x + 1, 6, COLOR_YELLOW);
		Painter::getInstance().drawPixel(surface, x, 7, COLOR_YELLOW);
		mChargeFrame++;
	}
}

void FunctionsPage::drawBatteryIcon(Surface& surface, int x, int y, int percent, bool charging) {
	(void) charging;
	Painter::getInstance().drawRect(surface, x, y, 24, 8, COLOR_WHITE, false);
	Painter::getInstance().drawRect(surface, x + 24, y + 2, 2, 4, COLOR_WHITE, true);
	int fillW = percent * 22 / 100;
	if (fillW > 0) {
		Color color = percent < 20 ? COLOR_RED : (percent < 50 ? COLOR_YELLOW : COLOR_GREEN);
		Painter::getInstance().drawRect(surface, x + 1, y + 1, fillW, 6, color, true);
	}
}

void FunctionsPage::drawIpAddress(Surface& surface, uint64_t currentMs) {
	const std::string ip = currentIpAddress(currentMs);
	const int textWidth = largeTextWidth(ip);
	const int cycleWidth = textWidth + DISPLAY_WIDTH + LARGE_CHAR_GAP;
	int scrollOffset = 0;
	if (cycleWidth > 0) {
		scrollOffset = static_cast<int>(
			((currentMs - mIpScrollStartMs) / IP_SCROLL_SPEED_MS) % cycleWidth);
	}

	int x = DISPLAY_WIDTH - scrollOffset;
	const int y = (DISPLAY_HEIGHT - LARGE_DIGIT_HEIGHT) / 2;
	for (size_t i = 0; i < ip.size(); ++i) {
		x += drawLargeChar(surface, x, y, ip[i], COLOR_WHITE);
		if (i < ip.size() - 1) {
			x += LARGE_CHAR_GAP;
		}
	}
}

void FunctionsPage::drawMicLight(Surface& surface) {
	const int previousPercent = mMicPercent;
	int raw = McuManager::getInstance().queryMicValue();
	if (raw >= 0) {
		const int percent = clampInt(raw * 100 / 65535, 0, 100);
		if (percent <= MIC_RAW_NOISE_GATE_PERCENT) {
			mMicPercent = 0;
		} else {
			mMicPercent = clampInt(
				(percent - MIC_RAW_NOISE_GATE_PERCENT) * 190 /
				(100 - MIC_RAW_NOISE_GATE_PERCENT) + 7,
				0, 100);
		}
	}
	if (mMicPercent <= MIC_IDLE_PERCENT) {
		mMicPercent = 0;
	} else {
		mMicHasTriggered = true;
	}
	mMicFrame += 2;

	const int punch = clampInt(mMicPercent - previousPercent, 0, 100);
	static const int attackSpeed[MIC_BAND_COUNT] = { 2, 4, 5, 7, 9 };
	static const int decaySpeed[MIC_BAND_COUNT] = { 1, 1, 2, 3, 4 };
	for (int band = 0; band < MIC_BAND_COUNT; ++band) {
		const int target = percentToRows(
			micBandTargetPercent(band, mMicPercent, punch, mMicFrame));
		if (target >= mMicBandLevels[band]) {
			mMicBandLevels[band] = clampInt(
				mMicBandLevels[band] + attackSpeed[band],
				1, target);
		} else {
			mMicBandLevels[band] = clampInt(
				mMicBandLevels[band] - decaySpeed[band],
				target, DISPLAY_HEIGHT);
		}
	}

	drawMicLightText(surface);

	for (int i = 0; i < MIC_BAR_COUNT; ++i) {
		const int target = micColumnTarget(i, mMicFrame, mMicBandLevels);
		if (target >= mMicColumnLevels[i]) {
			mMicColumnLevels[i] = target;
		} else {
			const int band = micBandForColumn(i);
			const int fallSpeed = 1 + band / 2 + ((i + mMicFrame) % 2);
			mMicColumnLevels[i] = clampInt(
				mMicColumnLevels[i] - fallSpeed, target, DISPLAY_HEIGHT);
		}

		if (mMicColumnLevels[i] >= mMicPeakLevels[i]) {
			mMicPeakLevels[i] = mMicColumnLevels[i];
		} else if (((mMicFrame + i) % 3) == 0) {
			mMicPeakLevels[i] = clampInt(
				mMicPeakLevels[i] - 1, mMicColumnLevels[i], DISPLAY_HEIGHT);
		}

		for (int y = DISPLAY_HEIGHT - mMicColumnLevels[i]; y < DISPLAY_HEIGHT; ++y) {
			const int rowFromBottom = DISPLAY_HEIGHT - 1 - y;
			Painter::getInstance().drawPixel(
				surface, i, y,
				micGradientColor(rowFromBottom, mMicPaletteIndex));
		}

		if (mMicHasTriggered && mMicPeakLevels[i] > 1) {
			Painter::getInstance().drawPixel(
				surface, i, DISPLAY_HEIGHT - mMicPeakLevels[i], COLOR_WHITE);
		}
	}
}

void FunctionsPage::drawMicLightText(Surface& surface) {
	drawGradientText(surface, "MUSIC", 7, mMicHasTriggered);
	drawGradientText(surface, "LIGHT", 13, mMicHasTriggered);
}

void FunctionsPage::drawGradientText(Surface& surface, const char* text, int y,
		bool dim) {
	const int len = static_cast<int>(strlen(text));
	if (len <= 0) {
		return;
	}

	const int width = Painter::getInstance().getTextWidth(text, 0);
	int x = (DISPLAY_WIDTH - width) / 2;
	for (int i = 0; i < len; ++i) {
		const char c = text[i];
		if (c < TomThumb.first || c > TomThumb.last) {
			x += 2;
			continue;
		}

		GFXglyph* glyph = &TomThumb.glyph[c - TomThumb.first];
		const uint8_t* bitmap = &TomThumb.bitmap[glyph->bitmapOffset];
		for (int gy = 0; gy < glyph->height; ++gy) {
			uint8_t rowBits = bitmap[gy];
			for (int gx = 0; gx < glyph->width; ++gx) {
				if ((rowBits & (0x80 >> gx)) == 0) {
					continue;
				}

				const int px = x + glyph->xOffset + gx;
				const int py = y + glyph->yOffset + gy;
				Color color = micGradientColor(
					DISPLAY_HEIGHT - 1 - py, mMicPaletteIndex);
				if (dim) {
					color = scaleColor(color, 18);
				}
				Painter::getInstance().drawPixel(surface, px, py, color);
			}
		}
		x += glyph->xAdvance;
	}
}

void FunctionsPage::updateMicReportState() {
	const bool shouldEnable = mCurrentFunction == FUNC_MIC_LIGHT;
	if (shouldEnable == mMicAutoReportEnabled) {
		return;
	}

	McuManager::getInstance().setAutoMicReport(shouldEnable);
	mMicAutoReportEnabled = shouldEnable;
	if (shouldEnable) {
		mMicPaletteIndex = (mMicPaletteIndex + 1 +
			static_cast<int>(nowMs() % MIC_PALETTE_COUNT)) % MIC_PALETTE_COUNT;
		mMicPercent = 0;
		mMicFrame = 0;
		mMicHasTriggered = false;
		for (int i = 0; i < MIC_BAR_COUNT; ++i) {
			mMicColumnLevels[i] = 1;
			mMicPeakLevels[i] = 1;
		}
		for (int i = 0; i < MIC_BAND_COUNT; ++i) {
			mMicBandLevels[i] = 1;
		}
	}
}

void FunctionsPage::updateWeatherState(bool force) {
	if (mCurrentFunction != FUNC_WEATHER) {
		return;
	}
	mWeatherService.requestRefresh(force);
}

void FunctionsPage::drawWeather(Surface& surface, uint64_t currentMs) {
	updateWeatherState(false);
	const WeatherService::Snapshot weather = mWeatherService.snapshot();
	mWeatherFrame++;

	if (!weather.hasWeather) {
		drawWeatherStatus(surface, weather, currentMs);
		return;
	}

	const int page = static_cast<int>(
		((currentMs - mWeatherPageStartMs) / WEATHER_PAGE_INTERVAL_MS) % 4);
	switch (page) {
	case 1:
		drawWeatherAtmosphere(surface, weather);
		break;
	case 2:
		drawWeatherAir(surface, weather);
		break;
	case 3:
		drawWeatherForecast(surface, weather);
		break;
	case 0:
	default:
		drawWeatherMain(surface, weather);
		break;
	}
}

void FunctionsPage::drawWeatherStatus(Surface& surface,
		const WeatherService::Snapshot& weather, uint64_t currentMs) {
	const bool timedOut = weather.refreshing &&
		mWeatherPageStartMs > 0 &&
		currentMs > mWeatherPageStartMs + 5000ULL;
	const bool loading = weather.refreshing && !timedOut;
	drawWeatherIcon(surface, 2, 0, loading ? 1 : 95, false);
	const char* top = timedOut ? "TIME" : (loading ? "WEATHER" : "NO DATA");
	const char* bottom = weather.status.c_str();
	drawRightText(surface, top, bottom, COLOR_WHITE,
		loading ? COLOR_CYAN : (timedOut ? COLOR_ORANGE : COLOR_RED));
}

void FunctionsPage::drawWeatherMain(Surface& surface,
		const WeatherService::Snapshot& weather) {
	drawWeatherIcon(surface, 1, 0, weather.weatherCode, false);

	const int temp = clampInt(weather.temperatureC, 0, 99);
	const bool twoDigits = temp >= 10;
	const int digitCount = twoDigits ? 2 : 1;
	const int digitWidth = digitCount * LARGE_DIGIT_WIDTH +
		(digitCount - 1) * LARGE_CHAR_GAP;
	const int cWidth = LARGE_DIGIT_WIDTH;
	const int totalWidth = digitWidth + 2 + cWidth;
	int x = 20 + (DISPLAY_WIDTH - 20 - totalWidth) / 2;
	const int y = 3;

	if (twoDigits) {
		drawScaledDigit(surface, x, y, temp / 10, LARGE_DIGIT_SCALE,
			COLOR_WHITE);
		x += LARGE_DIGIT_WIDTH + LARGE_CHAR_GAP;
	}
	drawScaledDigit(surface, x, y, temp % 10, LARGE_DIGIT_SCALE,
		COLOR_WHITE);
	x += LARGE_DIGIT_WIDTH + 2;
	drawLargeCelsius(surface, x, y, COLOR_YELLOW);

	if (weather.refreshing) {
		Painter::getInstance().drawPixel(
			surface, 51, (mWeatherFrame / 2) % DISPLAY_HEIGHT, COLOR_CYAN);
	}
}

void FunctionsPage::drawWeatherAtmosphere(Surface& surface,
		const WeatherService::Snapshot& weather) {
	drawHumidityWindIcon(surface, 1, 0);
	char top[14] = {0};
	snprintf(top, sizeof(top), "HUM %d%%", weather.humidityPercent);
	char bottom[14] = {0};
	if (weather.pressureHPa > 0) {
		snprintf(bottom, sizeof(bottom), "P %d", weather.pressureHPa);
	} else {
		snprintf(bottom, sizeof(bottom), "SPD %d", weather.windSpeedKmh);
	}
	drawRightText(surface, top, bottom, COLOR_SKY, COLOR_WHITE);
}

void FunctionsPage::drawWeatherAir(Surface& surface,
		const WeatherService::Snapshot& weather) {
	drawAirQualityIcon(surface, 1, 0, weather.aqi);
	if (!weather.hasAir) {
		drawRightText(surface, "AIR", "WAIT", COLOR_WHITE, COLOR_CYAN);
		return;
	}

	char top[14] = {0};
	snprintf(top, sizeof(top), "AQI %d", weather.aqi);
	char bottom[14] = {0};
	snprintf(bottom, sizeof(bottom), "PM %d", weather.pm25);
	drawRightText(surface, top, bottom, aqiColor(weather.aqi), COLOR_WHITE);

	const int barHeight = clampInt(weather.aqi, 0, 200) * 12 / 200;
	Painter::getInstance().drawRect(surface, 49, 3, 2, 12, COLOR_DIM, false);
	if (barHeight > 0) {
		Painter::getInstance().drawRect(surface, 49, 15 - barHeight, 2,
			barHeight, aqiColor(weather.aqi), true);
	}
}

void FunctionsPage::drawWeatherForecast(Surface& surface,
		const WeatherService::Snapshot& weather) {
	drawForecastIcon(surface, 1, 0, weather.weatherCode);
	char top[14] = {0};
	snprintf(top, sizeof(top), "H%d L%d", weather.tempMaxC, weather.tempMinC);
	char bottom[14] = {0};
	snprintf(bottom, sizeof(bottom), "RAIN %d", weather.precipitationPercent);
	drawRightText(surface, top, bottom, COLOR_ORANGE, COLOR_SKY);

	const int uvDots = clampInt(weather.uvIndex, 0, 10);
	for (int i = 0; i < uvDots && i < 10; ++i) {
		Painter::getInstance().drawPixel(surface, 22 + i * 2, 1,
			i < 5 ? COLOR_YELLOW : COLOR_RED);
	}
}

void FunctionsPage::drawWeatherIcon(Surface& surface, int x, int y, int code,
		bool isSmall) {
	switch (weatherKindForCode(code)) {
	case WEATHER_CLEAR:
		drawSunIcon(surface, x, y, isSmall);
		break;
	case WEATHER_CLOUD:
		drawSunIcon(surface, x, y, true);
		drawCloudIcon(surface, x, y + 1, isSmall);
		break;
	case WEATHER_FOG:
		drawFogIcon(surface, x, y, isSmall);
		break;
	case WEATHER_RAIN:
		drawRainIcon(surface, x, y, isSmall);
		break;
	case WEATHER_SNOW:
		drawSnowIcon(surface, x, y, isSmall);
		break;
	case WEATHER_THUNDER:
	default:
		drawThunderIcon(surface, x, y, isSmall);
		break;
	}
}

void FunctionsPage::drawSunIcon(Surface& surface, int x, int y, bool small) {
	const int cx = x + (small ? 5 : 8);
	const int cy = y + (small ? 5 : 8);
	const int radius = small ? 3 : 5;
	Painter::getInstance().drawCircle(surface, cx, cy, radius,
		COLOR_ORANGE, true);
	Painter::getInstance().drawCircle(surface, cx - 1, cy - 1,
		std::max(1, radius - 2), COLOR_YELLOW, true);

	static const int8_t rays[][4] = {
		{0, -7, 0, -5}, {5, -5, 4, -4}, {7, 0, 5, 0}, {5, 5, 4, 4},
		{0, 7, 0, 5}, {-5, 5, -4, 4}, {-7, 0, -5, 0}, {-5, -5, -4, -4}
	};
	for (int i = 0; i < 4; ++i) {
		const int ray = (i * 2 + mWeatherFrame / 3) % 8;
		Painter::getInstance().drawLine(surface,
			cx + rays[ray][0], cy + rays[ray][1],
			cx + rays[ray][2], cy + rays[ray][3], COLOR_YELLOW);
	}
	if ((mWeatherFrame % 6) < 3) {
		drawWeatherSparkle(surface, x + 14, y + 2, COLOR_WHITE);
	}
}

void FunctionsPage::drawCloudIcon(Surface& surface, int x, int y, bool small) {
	const int drift = (mWeatherFrame / 5) % 3;
	const int ox = x + drift - 1;
	const int baseY = y + (small ? 7 : 9);
	Painter::getInstance().drawCircle(surface, ox + 5, baseY, 3, COLOR_DIM, true);
	Painter::getInstance().drawCircle(surface, ox + 9, baseY - 2, 4,
		COLOR_WHITE, true);
	Painter::getInstance().drawCircle(surface, ox + 13, baseY, 3,
		COLOR_SKY, true);
	Painter::getInstance().drawRect(surface, ox + 4, baseY, 12, 4,
		COLOR_WHITE, true);
	Painter::getInstance().drawLine(surface, ox + 5, baseY + 4,
		ox + 15, baseY + 4, COLOR_SKY);
}

void FunctionsPage::drawRainIcon(Surface& surface, int x, int y, bool small) {
	drawCloudIcon(surface, x, y, small);
	for (int i = 0; i < 5; ++i) {
		const int dropX = x + 4 + i * 3;
		const int dropY = y + 10 + ((mWeatherFrame + i * 3) % 6);
		Painter::getInstance().drawPixel(surface, dropX, dropY,
			i % 2 == 0 ? COLOR_CYAN : COLOR_SKY);
		Painter::getInstance().drawPixel(surface, dropX - 1, dropY + 1,
			COLOR_BLUE);
	}
}

void FunctionsPage::drawThunderIcon(Surface& surface, int x, int y, bool small) {
	drawCloudIcon(surface, x, y, small);
	const Color boltColor = (mWeatherFrame % 8) < 4 ? COLOR_YELLOW : COLOR_WHITE;
	Painter::getInstance().drawLine(surface, x + 10, y + 8,
		x + 7, y + 13, boltColor);
	Painter::getInstance().drawLine(surface, x + 7, y + 13,
		x + 11, y + 12, boltColor);
	Painter::getInstance().drawLine(surface, x + 11, y + 12,
		x + 8, y + 16, boltColor);
	if ((mWeatherFrame % 10) < 3) {
		drawWeatherSparkle(surface, x + 16, y + 2, COLOR_PURPLE);
	}
}

void FunctionsPage::drawSnowIcon(Surface& surface, int x, int y, bool small) {
	drawCloudIcon(surface, x, y, small);
	for (int i = 0; i < 4; ++i) {
		const int sx = x + 5 + i * 4;
		const int sy = y + 10 + ((mWeatherFrame / 2 + i * 4) % 6);
		drawWeatherSparkle(surface, sx, sy, i % 2 ? COLOR_WHITE : COLOR_CYAN);
	}
}

void FunctionsPage::drawFogIcon(Surface& surface, int x, int y, bool small) {
	(void) small;
	drawCloudIcon(surface, x, y - 1, true);
	for (int row = 0; row < 3; ++row) {
		const int shift = (mWeatherFrame / 4 + row * 3) % 6;
		const int fy = y + 10 + row * 2;
		Painter::getInstance().drawLine(surface, x + shift, fy,
			x + 10 + shift, fy, row == 1 ? COLOR_WHITE : COLOR_SKY);
	}
}

void FunctionsPage::drawHumidityWindIcon(Surface& surface, int x, int y) {
	const int shimmer = (mWeatherFrame / 3) % 5;
	Painter::getInstance().drawLine(surface, x + 6, y + 2,
		x + 3, y + 7, COLOR_CYAN);
	Painter::getInstance().drawLine(surface, x + 6, y + 2,
		x + 9, y + 7, COLOR_CYAN);
	Painter::getInstance().drawCircle(surface, x + 6, y + 9, 4,
		COLOR_BLUE, true);
	Painter::getInstance().drawCircle(surface, x + 6, y + 8, 3,
		COLOR_CYAN, true);
	Painter::getInstance().drawPixel(surface, x + 5, y + 6 + shimmer / 2,
		COLOR_WHITE);

	for (int row = 0; row < 3; ++row) {
		const int fy = y + 4 + row * 4;
		const int drift = (mWeatherFrame / 2 + row * 2) % 5;
		const Color color = row == 1 ? COLOR_GREEN : COLOR_SKY;
		Painter::getInstance().drawLine(surface, x + 11, fy,
			x + 17, fy, color);
		Painter::getInstance().drawPixel(surface, x + 11 + drift, fy - 1,
			COLOR_WHITE);
		Painter::getInstance().drawPixel(surface, x + 17, fy + 1, color);
	}
}

void FunctionsPage::drawAirQualityIcon(Surface& surface, int x, int y,
		int aqi) {
	const Color quality = aqiColor(aqi);
	Painter::getInstance().drawLine(surface, x + 2, y + 14,
		x + 16, y + 14, COLOR_DIM);

	const int level = clampInt(aqi, 0, 200) * 10 / 200;
	for (int i = 0; i < 4; ++i) {
		const int barX = x + 3 + i * 3;
		const int barH = clampInt(level + 2 + i, 2, 12);
		const Color barColor =
			i == 0 ? COLOR_GREEN :
			(i == 1 ? COLOR_YELLOW : (i == 2 ? COLOR_ORANGE : COLOR_RED));
		Painter::getInstance().drawRect(surface, barX, y + 14 - barH,
			2, barH, i * 50 <= aqi ? barColor : scaleColor(barColor, 20), true);
	}

	for (int i = 0; i < 5; ++i) {
		const int px = x + 2 + ((mWeatherFrame + i * 5) % 15);
		const int py = y + 2 + ((i * 3 + mWeatherFrame / 2) % 8);
		Painter::getInstance().drawPixel(surface, px, py,
			i % 2 == 0 ? quality : COLOR_WHITE);
	}
	drawWeatherSparkle(surface, x + 16, y + 3, quality);
}

void FunctionsPage::drawForecastIcon(Surface& surface, int x, int y,
		int code) {
	Painter::getInstance().drawRect(surface, x + 2, y + 2,
		14, 12, COLOR_WHITE, false);
	Painter::getInstance().drawLine(surface, x + 2, y + 5,
		x + 15, y + 5, COLOR_SKY);
	Painter::getInstance().drawPixel(surface, x + 5, y + 1, COLOR_ORANGE);
	Painter::getInstance().drawPixel(surface, x + 12, y + 1, COLOR_ORANGE);

	const int pulse = (mWeatherFrame / 4) % 2;
	Painter::getInstance().drawCircle(surface, x + 6, y + 9, 3,
		COLOR_ORANGE, true);
	Painter::getInstance().drawPixel(surface, x + 6, y + 6 - pulse,
		COLOR_YELLOW);
	Painter::getInstance().drawPixel(surface, x + 3, y + 9, COLOR_YELLOW);

	if (weatherKindForCode(code) == WEATHER_RAIN ||
			weatherKindForCode(code) == WEATHER_THUNDER) {
		for (int i = 0; i < 3; ++i) {
			const int dropY = y + 8 + ((mWeatherFrame + i * 2) % 5);
			Painter::getInstance().drawPixel(surface, x + 10 + i * 2,
				dropY, COLOR_CYAN);
		}
	} else {
		const int drift = (mWeatherFrame / 5) % 2;
		Painter::getInstance().drawCircle(surface, x + 10 + drift, y + 10,
			2, COLOR_WHITE, true);
		Painter::getInstance().drawCircle(surface, x + 13 + drift, y + 9,
			2, COLOR_SKY, true);
		Painter::getInstance().drawRect(surface, x + 9 + drift, y + 10,
			6, 2, COLOR_WHITE, true);
	}
}

void FunctionsPage::drawWeatherSparkle(Surface& surface, int x, int y,
		const Color& color) {
	Painter::getInstance().drawPixel(surface, x, y, color);
	Painter::getInstance().drawPixel(surface, x - 1, y, color);
	Painter::getInstance().drawPixel(surface, x + 1, y, color);
	Painter::getInstance().drawPixel(surface, x, y - 1, color);
	Painter::getInstance().drawPixel(surface, x, y + 1, color);
}

void FunctionsPage::drawRightText(Surface& surface, const char* top,
		const char* bottom, const Color& topColor, const Color& bottomColor) {
	const int textX = 20;
	Painter::getInstance().drawText(surface, textX, 7, top, topColor, 0);
	Painter::getInstance().drawText(surface, textX, 14, bottom, bottomColor, 0);
}

std::string FunctionsPage::currentIpAddress(uint64_t currentMs) {
	if (currentMs - mLastIpQueryMs < IP_QUERY_INTERVAL_MS && !mCachedIp.empty()) {
		return mCachedIp;
	}

	mLastIpQueryMs = currentMs;
	const char* ip = WIFIMANAGER != nullptr ? WIFIMANAGER->getIp() : nullptr;
	if (ip != nullptr && ip[0] != '\0' && std::string(ip) != "0.0.0.0") {
		mCachedIp = ip;
	} else if (mCachedIp.empty()) {
		mCachedIp = "192.168.1.13";
	}
	return mCachedIp;
}

int FunctionsPage::drawLargeChar(Surface& surface, int x, int y, char c,
		const Color& color) {
	if (c >= '0' && c <= '9') {
		drawScaledDigit(surface, x, y, c - '0', LARGE_DIGIT_SCALE, color);
		return LARGE_DIGIT_WIDTH;
	}
	if (c == '.') {
		Painter::getInstance().drawRect(
			surface, x, y + LARGE_DIGIT_HEIGHT - DOT_WIDTH, DOT_WIDTH,
			DOT_WIDTH, color, true);
		return DOT_WIDTH;
	}
	return LARGE_DIGIT_SCALE;
}

void FunctionsPage::drawLargeCelsius(Surface& surface, int x, int y,
		const Color& color) {
	static const char* bitmap =
		"111"
		"100"
		"100"
		"100"
		"111";
	for (int row = 0; row < 5; ++row) {
		for (int col = 0; col < 3; ++col) {
			if (bitmap[row * 3 + col] != '1') {
				continue;
			}
			Painter::getInstance().drawRect(surface,
				x + col * LARGE_DIGIT_SCALE,
				y + row * LARGE_DIGIT_SCALE,
				LARGE_DIGIT_SCALE,
				LARGE_DIGIT_SCALE,
				color, true);
		}
	}
}

int FunctionsPage::largeCharWidth(char c) const {
	if (c >= '0' && c <= '9') {
		return LARGE_DIGIT_WIDTH;
	}
	if (c == '.') {
		return DOT_WIDTH;
	}
	return LARGE_DIGIT_SCALE;
}

int FunctionsPage::largeTextWidth(const std::string& text) const {
	int width = 0;
	for (size_t i = 0; i < text.size(); ++i) {
		width += largeCharWidth(text[i]);
		if (i < text.size() - 1) {
			width += LARGE_CHAR_GAP;
		}
	}
	return width;
}

void FunctionsPage::drawScaledDigit(Surface& surface, int x, int y,
		int digit, int scale, const Color& color) {
	if (digit < 0 || digit > 9 || scale <= 0) {
		return;
	}
	const char* bitmap = DIGITS[digit];
	for (int row = 0; row < 5; ++row) {
		for (int col = 0; col < 3; ++col) {
			if (bitmap[row * 3 + col] != '1') {
				continue;
			}
			Painter::getInstance().drawRect(
				surface, x + col * scale, y + row * scale,
				scale, scale, color, true);
		}
	}
}

void FunctionsPage::drawHeader(Surface& surface, const char* text) {
	Painter::getInstance().drawText(surface, 0, 4, text, COLOR_DIM, 0);
}

void FunctionsPage::drawCenteredText(Surface& surface, const char* text, int y,
		const Color& color) {
	const int width = Painter::getInstance().getTextWidth(text, 0);
	const int x = (DISPLAY_WIDTH - width) / 2;
	Painter::getInstance().drawText(surface, x, y, text, color, 0);
}

void FunctionsPage::drawTwoDigitNumber(Surface& surface, int x, int y, int value, const Color& color) {
	value = clampInt(value, 0, 99);
	drawDigit(surface, x, y - 10, value / 10, color);
	drawDigit(surface, x + 8, y - 10, value % 10, color);
}

void FunctionsPage::drawDigit(Surface& surface, int x, int y, int digit, const Color& color) {
	drawScaledDigit(surface, x, y, digit, 2, color);
}
