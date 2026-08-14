#ifndef MANAGERS_STREAMNOTICEINDICATOR_H_
#define MANAGERS_STREAMNOTICEINDICATOR_H_

#include <stdint.h>
#include <vector>

#include "system/Mutex.h"

typedef void (*stream_notice_callback_t)();

class StreamNoticeIndicator {
public:
	static StreamNoticeIndicator& getInstance();

	void setTopMenuVisible(bool visible);
	void setStreamDisplayEnabled(bool enabled);
	bool isStreamDisplayEnabled();
	bool isNoticeActive();
	void notifyStreamData();
	void setNoticeCallback(stream_notice_callback_t callback);
	void apply(std::vector<uint8_t>& rgbData);

private:
	StreamNoticeIndicator();
	~StreamNoticeIndicator();

	StreamNoticeIndicator(const StreamNoticeIndicator&) = delete;
	StreamNoticeIndicator& operator=(const StreamNoticeIndicator&) = delete;

	uint64_t nowMs() const;
	void setPixel(std::vector<uint8_t>& rgbData, int x, int y,
			uint8_t r, uint8_t g, uint8_t b);

	Mutex mMutex;
	bool mTopMenuVisible;
	bool mStreamDisplayEnabled;
	uint64_t mNoticeUntilMs;
	uint32_t mNoticeSequence;
	stream_notice_callback_t mNoticeCallback;
};

#endif
