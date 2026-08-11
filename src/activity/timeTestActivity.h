#ifndef __TIMETESTACTIVITY_H__
#define __TIMETESTACTIVITY_H__

#include "app/Activity.h"
#include "entry/EasyUIContext.h"
#include "uart/ProtocolData.h"
#include "uart/ProtocolParser.h"
#include "utils/Log.h"

class timeTestActivity : public Activity
{
public:
    timeTestActivity();
    virtual ~timeTestActivity();

    void registerUserTimer(int id, int time);
    void unregisterUserTimer(int id);
    void resetUserTimer(int id, int time);

protected:
    virtual const char* getAppName() const;
    virtual void onCreate();
    virtual void onResume();
    virtual void onPause();
    virtual void onIntent(const Intent *intentPtr);
    virtual bool onTimer(int id);

private:
    void rigesterActivityTimer();
};

#endif
