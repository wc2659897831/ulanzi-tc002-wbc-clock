#ifndef PAGES_FUNCTIONSPAGE_H_
#define PAGES_FUNCTIONSPAGE_H_

#include "pages/PageBase.h"
#include "assets/PetAnimationFrames.h"
#include "services/WeatherService.h"
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
		FUNC_PET,
		FUNC_AMBIENCE,
		FUNC_METRONOME,
		FUNC_BATTERY,
		FUNC_IP,
		FUNC_MIC_LIGHT,
		FUNC_WEATHER,
		FUNC_COUNT
	};

	enum AmbienceScene {
		AMBIENCE_FIRE = 0,
		AMBIENCE_RAIN,
		AMBIENCE_SNOW,
		AMBIENCE_STAR,
		AMBIENCE_WAVE,
		AMBIENCE_AURORA,
		AMBIENCE_BREATH,
		AMBIENCE_HEART,
		AMBIENCE_EQUALIZER,
		AMBIENCE_COUNT
	};

	enum PetAction {
		PET_AUTO = 0,
		PET_IDLE,
		PET_WALK,
		PET_HAPPY,
		PET_EAT,
		PET_SLEEP,
		PET_COUNT
	};

	int mCurrentFunction;
	bool mPomodoroRunning;
	bool mPomodoroResting;
	int mPomodoroRemainingSec;
	uint64_t mPomodoroLastTickMs;
	uint64_t mMiddlePressStartMs;
	bool mMiddleLongResetDone;
	int mPetSpeciesIndex;
	int mPetAction;
	const PetAnimationAssets::Animation* mPetAnimation;
	int mPetFrameIndex;
	uint64_t mPetLastFrameMs;
	uint64_t mPetMiddlePressStartMs;
	bool mPetMiddleLongSpeciesDone;
	uint64_t mPetLeftPressStartMs;
	bool mPetLeftPressed;
	uint64_t mPetRightPressStartMs;
	bool mPetRightPressed;
	int mPetLabelFrame;
	int mAmbienceScene;
	int mAmbienceFrame;
	bool mAmbienceSceneSelectMode;
	bool mMetronomeRunning;
	int mMetronomeBpm;
	int mMetronomeFrame;
	uint64_t mMetronomeLastBeatMs;
	uint64_t mMetronomeBeatFlashMs;
	int mChargeFrame;
	int mTomatoFrame;
	int mMicPercent;
	int mMicFrame;
	int mMicPaletteIndex;
	int mMicColumnLevels[52];
	int mMicPeakLevels[52];
	int mMicBandLevels[5];
	bool mMicHasTriggered;
	bool mMicAutoReportEnabled;
	uint64_t mIpScrollStartMs;
	std::string mCachedIp;
	uint64_t mLastIpQueryMs;
	WeatherService mWeatherService;
	int mWeatherFrame;
	uint64_t mWeatherPageStartMs;

	uint64_t nowMs() const;
	void switchFunction(int delta);
	void togglePomodoro();
	void resetPomodoro();
	void advancePomodoroPhase();
	void updatePomodoro(uint64_t currentMs);
	void updateMiddleLongPress(uint64_t currentMs);
	void updateMetronome(uint64_t currentMs);
	void drawPomodoro(Surface& surface);
	void drawTomatoIcon(Surface& surface, int x, int y);
	void switchPetSpecies(int delta);
	void selectPetAction(int action);
	const PetAnimationAssets::PetAnimationSet& currentPetSet() const;
	const PetAnimationAssets::Animation* petAnimationForAction(int action) const;
	void updatePetAnimation(uint64_t currentMs);
	void drawPet(Surface& surface);
	void drawPetFrame(Surface& surface, const PetAnimationAssets::Frame& frame);
	void drawPetLabel(Surface& surface);
	void switchAmbienceScene(int delta);
	void drawAmbience(Surface& surface);
	void drawFireAmbience(Surface& surface);
	void drawRainAmbience(Surface& surface);
	void drawSnowAmbience(Surface& surface);
	void drawStarAmbience(Surface& surface);
	void drawWaveAmbience(Surface& surface);
	void drawAuroraAmbience(Surface& surface);
	void drawBreathAmbience(Surface& surface);
	void drawHeartAmbience(Surface& surface);
	void drawEqualizerAmbience(Surface& surface);
	void drawAmbienceSceneName(Surface& surface);
	void drawMetronome(Surface& surface, uint64_t currentMs);
	void drawMetronomeNeedle(Surface& surface, int phase, const Color& color);
	void drawBattery(Surface& surface);
	void drawBatteryIcon(Surface& surface, int x, int y, int percent, bool charging);
	void drawIpAddress(Surface& surface, uint64_t currentMs);
	void drawMicLight(Surface& surface);
	void drawMicLightText(Surface& surface);
	void drawGradientText(Surface& surface, const char* text, int y, bool dim);
	void updateMicReportState();
	void updateWeatherState(bool force);
	void drawWeather(Surface& surface, uint64_t currentMs);
	void drawWeatherStatus(Surface& surface,
			const WeatherService::Snapshot& weather, uint64_t currentMs);
	void drawWeatherMain(Surface& surface,
			const WeatherService::Snapshot& weather);
	void drawWeatherAtmosphere(Surface& surface,
			const WeatherService::Snapshot& weather);
	void drawWeatherAir(Surface& surface,
			const WeatherService::Snapshot& weather);
	void drawWeatherForecast(Surface& surface,
			const WeatherService::Snapshot& weather);
	void drawWeatherIcon(Surface& surface, int x, int y, int code,
			bool isSmall);
	void drawSunIcon(Surface& surface, int x, int y, bool small);
	void drawCloudIcon(Surface& surface, int x, int y, bool small);
	void drawRainIcon(Surface& surface, int x, int y, bool small);
	void drawThunderIcon(Surface& surface, int x, int y, bool small);
	void drawSnowIcon(Surface& surface, int x, int y, bool small);
	void drawFogIcon(Surface& surface, int x, int y, bool small);
	void drawHumidityWindIcon(Surface& surface, int x, int y);
	void drawAirQualityIcon(Surface& surface, int x, int y, int aqi);
	void drawForecastIcon(Surface& surface, int x, int y, int code);
	void drawWeatherSparkle(Surface& surface, int x, int y,
			const Color& color);
	void drawRightText(Surface& surface, const char* top, const char* bottom,
			const Color& topColor, const Color& bottomColor);
	std::string currentIpAddress(uint64_t currentMs);
	int drawLargeChar(Surface& surface, int x, int y, char c, const Color& color);
	void drawLargeCelsius(Surface& surface, int x, int y, const Color& color);
	void drawScaledDigit(Surface& surface, int x, int y, int digit,
			int scale, const Color& color);
	int largeCharWidth(char c) const;
	int largeTextWidth(const std::string& text) const;
	void drawHeader(Surface& surface, const char* text);
	void drawCenteredText(Surface& surface, const char* text, int y, const Color& color);
	void drawTwoDigitNumber(Surface& surface, int x, int y, int value, const Color& color);
	void drawDigit(Surface& surface, int x, int y, int digit, const Color& color);
};

#endif
