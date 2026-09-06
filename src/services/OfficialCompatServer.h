#ifndef SERVICES_OFFICIAL_COMPAT_SERVER_H_
#define SERVICES_OFFICIAL_COMPAT_SERVER_H_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class OfficialCompatServer {
public:
    static OfficialCompatServer& getInstance();

    OfficialCompatServer(const OfficialCompatServer&) = delete;
    OfficialCompatServer& operator=(const OfficialCompatServer&) = delete;

    void start();
    void stop();
    void stopDisplay();

private:
    struct DecodedFrame {
        int width;
        int height;
        int delayMs;
        std::vector<unsigned char> rgb;
    };

    struct DeviceConfig {
        int brightness;
        int lowBrightness;
        int midBrightness;
        int highBrightness;
        int volume;
        int carouselSpeed;
        int scrollSpeed;
        std::string timezone;
        std::string dateFormat;
        bool showWeek;
        int weekStart;
        bool lowBatteryAutoSleep;
        std::string wifiSsid;
        std::string wifiPassword;
        bool wifiConnect;
    };

    OfficialCompatServer();
    ~OfficialCompatServer();

    void serverLoop();
    void handleClient(int clientFd);
    void handleCustom(int clientFd, const std::string& target,
            const std::string& body);

    std::string baseJson() const;
    std::string configJson() const;
    void setConfig(const std::string& body);
    bool persistConfigLocked() const;

    bool readChunkedBody(int clientFd, std::string& body,
            const std::string& initialBody) const;
    bool decodeGifImage(const std::vector<unsigned char>& encoded,
            std::vector<DecodedFrame>& frames) const;
    bool decodePngImage(const std::vector<unsigned char>& encoded,
            std::vector<DecodedFrame>& frames) const;
    bool decodeImage(const std::vector<unsigned char>& encoded,
            const std::string& mime, std::vector<DecodedFrame>& frames) const;
    bool decodeStillImage(const std::vector<unsigned char>& encoded,
            int codecId, std::vector<DecodedFrame>& frames) const;
    bool decodeImageDataUri(const std::string& dataUri,
            std::vector<DecodedFrame>& frames) const;
    static std::string extractQueryValue(const std::string& target,
            const std::string& key);
    static std::string urlDecode(const std::string& value);
    static std::string jsonEscape(const std::string& value);
    static std::string stripBase64Whitespace(const std::string& value);
    static bool sendAll(int fd, const char* data, size_t size);
    static void sendResponse(int fd, int status, const char* contentType,
            const std::string& body);
    static void sendRedirect(int fd, const char* location);
    static bool pasteFrame(const DecodedFrame& frame, int x, int y,
            std::vector<unsigned char>& framebuffer);

    void clearDisplay();
    void showDecodeError();
    void stopAnimation();
    void displayFrames(const std::vector<DecodedFrame>& frames,
            int positionX, int positionY, int durationSeconds);

private:
    static const int kPort = 80;
    static const int kDisplayWidth = 52;
    static const int kDisplayHeight = 16;
    static const int kMaxRequestBody = 4 * 1024 * 1024;

    std::atomic<bool> mRunning;
    int mListenFd;
    std::thread mServerThread;

    std::atomic<bool> mAnimationRunning;
    bool mAnimationStopRequested;
    std::thread mAnimationThread;
    mutable std::mutex mAnimationMutex;
    std::condition_variable mAnimationCondition;

    mutable std::mutex mConfigMutex;
    DeviceConfig mConfig;
};

#endif
