#ifndef SERVICES_MQTTSTATUSSERVICE_H_
#define SERVICES_MQTTSTATUSSERVICE_H_

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class MqttStatusService {
public:
	enum CodexStatus {
		STATUS_OFF = 0,
		STATUS_IDLE,
		STATUS_WORKING,
		STATUS_ATTENTION,
		STATUS_BLOCKED,
		STATUS_UNKNOWN
	};

	struct Config {
		std::string host;
		int port;
		std::string topic;
		std::string clientId;
		std::string username;
		std::string password;
		bool enabled;
	};

	struct Snapshot {
		bool configured;
		bool connected;
		CodexStatus status;
		std::string statusText;
		std::string message;
		std::string topic;
		std::string broker;
		unsigned int lastSeenSeconds;
	};

	static MqttStatusService& getInstance();

	MqttStatusService(const MqttStatusService&) = delete;
	MqttStatusService& operator=(const MqttStatusService&) = delete;

	void configure(const Config& config);
	void start();
	void stop();
	Snapshot snapshot() const;

	static Config defaultConfig();
	static CodexStatus parseStatus(const std::string& value);
	static const char* statusName(CodexStatus status);

private:
	MqttStatusService();
	~MqttStatusService();

	void workerLoop();
	bool connectAndSubscribe();
	bool sendConnect();
	bool sendSubscribe();
	bool sendPing();
	bool readPacket(unsigned char& packetType,
			std::vector<unsigned char>& body);
	bool handlePublish(unsigned char flags,
			const std::vector<unsigned char>& body);
	void handleMessage(const std::string& payload);
	void setConnected(bool connected);
	void setStatus(CodexStatus status, const std::string& message);
	void markDisconnectedStatus();

	static bool sendAll(int fd, const unsigned char* data, size_t size);
	static bool recvAll(int fd, unsigned char* data, size_t size);
	static void appendUtf8(std::vector<unsigned char>& output,
			const std::string& value);
	static void appendRemainingLength(std::vector<unsigned char>& output,
			size_t length);
	static std::string trimCopy(const std::string& value);
	static std::string lowerCopy(const std::string& value);
	static unsigned int nowSeconds();

private:
	mutable std::mutex mMutex;
	Config mConfig;
	bool mConnected;
	CodexStatus mStatus;
	std::string mMessage;
	unsigned int mLastSeenSeconds;

	std::atomic<bool> mRunning;
	int mSocketFd;
	unsigned short mPacketId;
	std::thread mThread;
};

#endif
