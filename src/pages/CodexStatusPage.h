#ifndef PAGES_CODEXSTATUSPAGE_H_
#define PAGES_CODEXSTATUSPAGE_H_

#include "pages/PageBase.h"
#include "services/MqttStatusService.h"
#include "utils/Painter.h"
#include "utils/Surface.h"

#include <stdint.h>

class CodexStatusPage : public PageBase {
public:
	CodexStatusPage();
	virtual ~CodexStatusPage();

	virtual void draw() override;
	virtual void onEnter() override;
	virtual void onExit() override;
	virtual bool onKeyEvent(int keyCode, int keyStatus) override;
	bool needsFastRefresh() const;

private:
	uint64_t nowMs() const;
	void drawStatusLights(Surface& surface,
			const MqttStatusService::Snapshot& snapshot, uint64_t currentMs);
	void drawLight(Surface& surface, int cx, int cy, const Color& color,
			bool active);
	void drawStatusText(Surface& surface,
			const MqttStatusService::Snapshot& snapshot);
	void drawNoConfig(Surface& surface);
	void drawDisconnected(Surface& surface,
			const MqttStatusService::Snapshot& snapshot);

private:
	int mFrame;
};

#endif
