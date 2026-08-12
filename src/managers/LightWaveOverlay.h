#ifndef MANAGERS_LIGHTWAVEOVERLAY_H_
#define MANAGERS_LIGHTWAVEOVERLAY_H_

#include <stdint.h>
#include <vector>

#include "system/Mutex.h"
#include "utils/Surface.h"

class LightWaveOverlay {
public:
	enum Direction {
		DIRECTION_FROM_BOTTOM = 0,
		DIRECTION_FROM_LEFT,
		DIRECTION_FROM_TOP,
		DIRECTION_FROM_RIGHT
	};

	enum DiagonalDirection {
		DIAGONAL_LEFT_TOP_TO_RIGHT_BOTTOM = 0,
		DIAGONAL_RIGHT_TOP_TO_LEFT_BOTTOM
	};

	static LightWaveOverlay& getInstance();

	void trigger(Direction direction);
	void triggerDiagonal(DiagonalDirection direction);
	void updateBaseFrame(const std::vector<uint8_t>& rgbData);
	void apply(std::vector<uint8_t>& rgbData);
	void sendFrame(const std::vector<uint8_t>& rgbData);

private:
	class AnimationThread;
	enum WaveType {
		WAVE_RADIAL = 0,
		WAVE_DIAGONAL_BAR
	};

	struct WaveState {
		WaveType type;
		Direction direction;
		DiagonalDirection diagonalDirection;
		int frame;
		Color spark;
		Color core;
		Color inner;
		Color outer;
		Color glow;
	};

	LightWaveOverlay();
	~LightWaveOverlay();

	bool animationThreadLoop();
	WaveState createWave(Direction direction);
	WaveState createDiagonalWave(DiagonalDirection direction);
	uint32_t nextRandom();
	void overlayWaves(std::vector<uint8_t>& rgbData);
	void overlayWave(std::vector<uint8_t>& rgbData, const WaveState& wave);
	void overlayRadialWave(std::vector<uint8_t>& rgbData, const WaveState& wave);
	void overlayDiagonalBar(std::vector<uint8_t>& rgbData, const WaveState& wave);
	void advanceWaves();
	void originForDirection(Direction direction, int& originX2, int& originY2) const;
	int maxRadiusSq4FromOrigin(int originX2, int originY2) const;
	void setPixelAdd(std::vector<uint8_t>& rgbData, int x, int y,
		const Color& color, int strength, int maxStrength);
	Color scaleColor(const Color& color, int strength, int maxStrength);

	LightWaveOverlay(const LightWaveOverlay&) = delete;
	LightWaveOverlay& operator=(const LightWaveOverlay&) = delete;

	Mutex mMutex;
	std::vector<uint8_t> mBaseFrame;
	std::vector<WaveState> mWaves;
	uint32_t mRandomSeed;
	bool mThreadRunning;
	AnimationThread* mThread;
};

#endif
