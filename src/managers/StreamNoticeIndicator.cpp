#include "managers/StreamNoticeIndicator.h"

#include <sys/time.h>

namespace {

#define DISPLAY_WIDTH 52
#define DISPLAY_HEIGHT 16
#define NOTICE_X (DISPLAY_WIDTH - 1)
#define NOTICE_Y (DISPLAY_HEIGHT - 1)
#define NOTICE_DURATION_MS 900

} // namespace

StreamNoticeIndicator& StreamNoticeIndicator::getInstance() {
	static StreamNoticeIndicator instance;
	return instance;
}

StreamNoticeIndicator::StreamNoticeIndicator()
	: mTopMenuVisible(false),
	  mStreamDisplayEnabled(false),
	  mNoticeUntilMs(0),
	  mNoticeCallback(nullptr) {
}

StreamNoticeIndicator::~StreamNoticeIndicator() {
}

void StreamNoticeIndicator::setTopMenuVisible(bool visible) {
	Mutex::Autolock lock(mMutex);
	mTopMenuVisible = visible;
	if (!visible) {
		mNoticeUntilMs = 0;
	}
}

void StreamNoticeIndicator::setStreamDisplayEnabled(bool enabled) {
	Mutex::Autolock lock(mMutex);
	mStreamDisplayEnabled = enabled;
	if (enabled) {
		mNoticeUntilMs = 0;
	}
}

bool StreamNoticeIndicator::isStreamDisplayEnabled() {
	Mutex::Autolock lock(mMutex);
	return mStreamDisplayEnabled;
}

void StreamNoticeIndicator::notifyStreamData() {
	stream_notice_callback_t callback = nullptr;
	{
		Mutex::Autolock lock(mMutex);
		if (mStreamDisplayEnabled) {
			return;
		}
		mNoticeUntilMs = nowMs() + NOTICE_DURATION_MS;
		callback = mNoticeCallback;
	}

	if (callback) {
		callback();
	}
}

void StreamNoticeIndicator::setNoticeCallback(stream_notice_callback_t callback) {
	Mutex::Autolock lock(mMutex);
	mNoticeCallback = callback;
}

void StreamNoticeIndicator::apply(std::vector<uint8_t>& rgbData) {
	Mutex::Autolock lock(mMutex);
	if (!mTopMenuVisible || mStreamDisplayEnabled ||
			nowMs() > mNoticeUntilMs ||
			rgbData.size() < DISPLAY_WIDTH * DISPLAY_HEIGHT * 3) {
		return;
	}
	setPixel(rgbData, NOTICE_X, NOTICE_Y, 255, 40, 180);
}

uint64_t StreamNoticeIndicator::nowMs() const {
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	return static_cast<uint64_t>(tv.tv_sec) * 1000ULL +
		static_cast<uint64_t>(tv.tv_usec) / 1000ULL;
}

void StreamNoticeIndicator::setPixel(std::vector<uint8_t>& rgbData, int x, int y,
		uint8_t r, uint8_t g, uint8_t b) {
	if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) {
		return;
	}
	const int index = (y * DISPLAY_WIDTH + x) * 3;
	rgbData[index] = r;
	rgbData[index + 1] = g;
	rgbData[index + 2] = b;
}
