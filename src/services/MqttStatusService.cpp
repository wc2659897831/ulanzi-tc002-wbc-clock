#include "services/MqttStatusService.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <sstream>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <base/base.h>
#include <base/json_object.h>

namespace {

#define MQTT_KEEPALIVE_SECONDS 30
#define MQTT_RECONNECT_DELAY_US (3 * 1000 * 1000)
#define MQTT_PACKET_MAX_SIZE (128 * 1024)

} // namespace

MqttStatusService& MqttStatusService::getInstance() {
	static MqttStatusService instance;
	return instance;
}

MqttStatusService::MqttStatusService()
	: mConfig(defaultConfig()),
	  mConnected(false),
	  mStatus(STATUS_OFF),
	  mLastSeenSeconds(0),
	  mRunning(false),
	  mSocketFd(-1),
	  mPacketId(1) {
}

MqttStatusService::~MqttStatusService() {
	stop();
}

MqttStatusService::Config MqttStatusService::defaultConfig() {
	Config config;
	config.host = "";
	config.port = 1883;
	config.topic = "tc002/codex/status";
	config.clientId = "tc002-codex";
	config.username = "";
	config.password = "";
	config.enabled = false;
	return config;
}

void MqttStatusService::configure(const Config& config) {
	std::lock_guard<std::mutex> lock(mMutex);
	mConfig = config;
	mConfig.enabled = config.enabled && !config.host.empty() &&
		!config.topic.empty();
	mConnected = false;
	if (mConfig.clientId.empty()) {
		mConfig.clientId = "tc002-codex";
	}
	if (mConfig.port <= 0) {
		mConfig.port = 1883;
	}
	if (mConfig.topic.empty()) {
		mConfig.topic = "tc002/codex/status";
	}
}

void MqttStatusService::start() {
	if (mRunning.exchange(true)) {
		return;
	}
	mThread = std::thread(&MqttStatusService::workerLoop, this);
}

void MqttStatusService::stop() {
	if (!mRunning.exchange(false)) {
		return;
	}
	if (mSocketFd >= 0) {
		shutdown(mSocketFd, SHUT_RDWR);
		close(mSocketFd);
		mSocketFd = -1;
	}
	if (mThread.joinable()) {
		mThread.join();
	}
}

MqttStatusService::Snapshot MqttStatusService::snapshot() const {
	std::lock_guard<std::mutex> lock(mMutex);
	Snapshot snapshot;
	snapshot.configured = mConfig.enabled;
	snapshot.connected = mConnected;
	snapshot.status = mStatus;
	snapshot.statusText = statusName(mStatus);
	snapshot.message = mMessage;
	snapshot.topic = mConfig.topic;
	snapshot.broker = mConfig.host;
	if (mConfig.port > 0 && !mConfig.host.empty()) {
		snapshot.broker += ":" + std::to_string(mConfig.port);
	}
	const unsigned int now = nowSeconds();
	snapshot.lastSeenSeconds = mLastSeenSeconds > 0 && now >= mLastSeenSeconds ?
		now - mLastSeenSeconds : 0;
	return snapshot;
}

void MqttStatusService::workerLoop() {
	while (mRunning) {
		Config config;
		{
			std::lock_guard<std::mutex> lock(mMutex);
			config = mConfig;
		}

		if (!config.enabled) {
			setConnected(false);
			usleep(MQTT_RECONNECT_DELAY_US);
			continue;
		}

		if (!connectAndSubscribe()) {
			setConnected(false);
			if (mSocketFd >= 0) {
				close(mSocketFd);
				mSocketFd = -1;
			}
			usleep(MQTT_RECONNECT_DELAY_US);
			continue;
		}

		setConnected(true);
		unsigned int lastPingSeconds = nowSeconds();
		while (mRunning) {
			fd_set readSet;
			FD_ZERO(&readSet);
			FD_SET(mSocketFd, &readSet);
			timeval timeout;
			timeout.tv_sec = 1;
			timeout.tv_usec = 0;
			const int selected = select(mSocketFd + 1, &readSet, nullptr,
				nullptr, &timeout);
			if (selected < 0) {
				LOGW_TRACE("MqttStatusService: select failed: %s",
					strerror(errno));
				break;
			}
			if (selected > 0 && FD_ISSET(mSocketFd, &readSet)) {
				unsigned char packetType = 0;
				std::vector<unsigned char> body;
				if (!readPacket(packetType, body)) {
					break;
				}
				const unsigned char type = packetType >> 4;
				if (type == 3) {
					handlePublish(packetType & 0x0f, body);
				}
			}

			const unsigned int now = nowSeconds();
			if (now >= lastPingSeconds + MQTT_KEEPALIVE_SECONDS / 2) {
				if (!sendPing()) {
					break;
				}
				lastPingSeconds = now;
			}
		}

		setConnected(false);
		if (mSocketFd >= 0) {
			close(mSocketFd);
			mSocketFd = -1;
		}
		usleep(MQTT_RECONNECT_DELAY_US);
	}
}

bool MqttStatusService::connectAndSubscribe() {
	Config config;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		config = mConfig;
	}

	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	addrinfo* results = nullptr;
	const std::string port = std::to_string(config.port);
	const int gai = getaddrinfo(config.host.c_str(), port.c_str(), &hints,
		&results);
	if (gai != 0 || results == nullptr) {
		LOGW_TRACE("MqttStatusService: resolve %s failed",
			config.host.c_str());
		return false;
	}

	bool connected = false;
	for (addrinfo* item = results; item != nullptr; item = item->ai_next) {
		mSocketFd = socket(item->ai_family, item->ai_socktype,
			item->ai_protocol);
		if (mSocketFd < 0) {
			continue;
		}
		if (connect(mSocketFd, item->ai_addr, item->ai_addrlen) == 0) {
			connected = true;
			break;
		}
		close(mSocketFd);
		mSocketFd = -1;
	}
	freeaddrinfo(results);

	if (!connected) {
		LOGW_TRACE("MqttStatusService: connect %s:%d failed",
			config.host.c_str(), config.port);
		return false;
	}

	if (!sendConnect()) {
		return false;
	}

	unsigned char packetType = 0;
	std::vector<unsigned char> body;
	if (!readPacket(packetType, body) || (packetType >> 4) != 2 ||
			body.size() < 2 || body[1] != 0) {
		LOGW_TRACE("MqttStatusService: MQTT CONNACK failed");
		return false;
	}

	if (!sendSubscribe()) {
		return false;
	}

	if (!readPacket(packetType, body) || (packetType >> 4) != 9) {
		LOGW_TRACE("MqttStatusService: MQTT SUBACK failed");
		return false;
	}

	LOGI_TRACE("MqttStatusService: subscribed %s",
		config.topic.c_str());
	return true;
}

bool MqttStatusService::sendConnect() {
	Config config;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		config = mConfig;
	}

	std::vector<unsigned char> body;
	appendUtf8(body, "MQTT");
	body.push_back(4);
	unsigned char flags = 0x02;
	if (!config.username.empty()) {
		flags |= 0x80;
	}
	if (!config.password.empty()) {
		flags |= 0x40;
	}
	body.push_back(flags);
	body.push_back(0);
	body.push_back(MQTT_KEEPALIVE_SECONDS);
	appendUtf8(body, config.clientId);
	if (!config.username.empty()) {
		appendUtf8(body, config.username);
	}
	if (!config.password.empty()) {
		appendUtf8(body, config.password);
	}

	std::vector<unsigned char> packet;
	packet.push_back(0x10);
	appendRemainingLength(packet, body.size());
	packet.insert(packet.end(), body.begin(), body.end());
	return sendAll(mSocketFd, packet.data(), packet.size());
}

bool MqttStatusService::sendSubscribe() {
	Config config;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		config = mConfig;
	}

	std::vector<unsigned char> body;
	const unsigned short packetId = mPacketId++;
	body.push_back(static_cast<unsigned char>((packetId >> 8) & 0xff));
	body.push_back(static_cast<unsigned char>(packetId & 0xff));
	appendUtf8(body, config.topic);
	body.push_back(0);

	std::vector<unsigned char> packet;
	packet.push_back(0x82);
	appendRemainingLength(packet, body.size());
	packet.insert(packet.end(), body.begin(), body.end());
	return sendAll(mSocketFd, packet.data(), packet.size());
}

bool MqttStatusService::sendPing() {
	const unsigned char packet[] = { 0xc0, 0x00 };
	return sendAll(mSocketFd, packet, sizeof(packet));
}

bool MqttStatusService::readPacket(unsigned char& packetType,
		std::vector<unsigned char>& body) {
	body.clear();
	if (!recvAll(mSocketFd, &packetType, 1)) {
		return false;
	}

	size_t remainingLength = 0;
	size_t multiplier = 1;
	for (int i = 0; i < 4; ++i) {
		unsigned char encoded = 0;
		if (!recvAll(mSocketFd, &encoded, 1)) {
			return false;
		}
		remainingLength += static_cast<size_t>(encoded & 127) * multiplier;
		if ((encoded & 128) == 0) {
			break;
		}
		multiplier *= 128;
		if (i == 3) {
			return false;
		}
	}
	if (remainingLength > MQTT_PACKET_MAX_SIZE) {
		LOGW_TRACE("MqttStatusService: packet too large %d",
			static_cast<int>(remainingLength));
		return false;
	}
	body.resize(remainingLength);
	return remainingLength == 0 ||
		recvAll(mSocketFd, body.data(), remainingLength);
}

bool MqttStatusService::handlePublish(unsigned char flags,
		const std::vector<unsigned char>& body) {
	if (body.size() < 2) {
		return false;
	}
	const size_t topicLength =
		(static_cast<size_t>(body[0]) << 8) | static_cast<size_t>(body[1]);
	if (body.size() < 2 + topicLength) {
		return false;
	}
	size_t payloadOffset = 2 + topicLength;
	const unsigned char qos = (flags >> 1) & 0x03;
	if (qos > 0) {
		if (body.size() < payloadOffset + 2) {
			return false;
		}
		payloadOffset += 2;
	}
	const std::string payload(body.begin() + payloadOffset, body.end());
	handleMessage(payload);
	return true;
}

void MqttStatusService::handleMessage(const std::string& payload) {
	std::string statusValue = trimCopy(payload);
	std::string message;

	try {
		const base::JSONObject root = base::JSONObject::parse(payload);
		statusValue = root.getString("status", "");
		if (statusValue.empty()) {
			statusValue = root.getString("state", "");
		}
		message = root.getString("message", "");
		if (message.empty()) {
			message = root.getString("task", "");
		}
	} catch (...) {
		message = "";
	}

	CodexStatus status = parseStatus(statusValue);
	if (status == STATUS_UNKNOWN) {
		status = parseStatus(payload);
	}
	setStatus(status, message.empty() ? trimCopy(payload) : message);
}

void MqttStatusService::setConnected(bool connected) {
	std::lock_guard<std::mutex> lock(mMutex);
	mConnected = connected;
}

void MqttStatusService::setStatus(CodexStatus status,
		const std::string& message) {
	std::lock_guard<std::mutex> lock(mMutex);
	mStatus = status;
	mMessage = message;
	mLastSeenSeconds = nowSeconds();
	LOGI_TRACE("MqttStatusService: status=%s message=%s",
		statusName(status), message.c_str());
}

MqttStatusService::CodexStatus MqttStatusService::parseStatus(
		const std::string& value) {
	const std::string status = lowerCopy(trimCopy(value));
	if (status.empty() || status == "off") {
		return STATUS_OFF;
	}
	if (status == "idle" || status == "done" || status == "complete" ||
			status == "completed" || status == "success") {
		return STATUS_IDLE;
	}
	if (status == "working" || status == "running" ||
			status == "in_progress" || status == "busy") {
		return STATUS_WORKING;
	}
	if (status == "attention" || status == "waiting" ||
			status == "needs_input" || status == "approval" ||
			status == "thinking") {
		return STATUS_ATTENTION;
	}
	if (status == "blocked" || status == "error" || status == "failed" ||
			status == "failure") {
		return STATUS_BLOCKED;
	}
	return STATUS_UNKNOWN;
}

const char* MqttStatusService::statusName(CodexStatus status) {
	switch (status) {
	case STATUS_OFF:
		return "off";
	case STATUS_IDLE:
		return "idle";
	case STATUS_WORKING:
		return "working";
	case STATUS_ATTENTION:
		return "attention";
	case STATUS_BLOCKED:
		return "blocked";
	case STATUS_UNKNOWN:
	default:
		return "unknown";
	}
}

bool MqttStatusService::sendAll(int fd, const unsigned char* data,
		size_t size) {
	size_t sent = 0;
	while (sent < size) {
		const ssize_t result = send(fd, data + sent, size - sent, 0);
		if (result <= 0) {
			return false;
		}
		sent += static_cast<size_t>(result);
	}
	return true;
}

bool MqttStatusService::recvAll(int fd, unsigned char* data, size_t size) {
	size_t received = 0;
	while (received < size) {
		const ssize_t result = recv(fd, data + received, size - received, 0);
		if (result <= 0) {
			return false;
		}
		received += static_cast<size_t>(result);
	}
	return true;
}

void MqttStatusService::appendUtf8(std::vector<unsigned char>& output,
		const std::string& value) {
	const size_t length = value.size();
	output.push_back(static_cast<unsigned char>((length >> 8) & 0xff));
	output.push_back(static_cast<unsigned char>(length & 0xff));
	output.insert(output.end(), value.begin(), value.end());
}

void MqttStatusService::appendRemainingLength(
		std::vector<unsigned char>& output, size_t length) {
	do {
		unsigned char encoded = static_cast<unsigned char>(length % 128);
		length /= 128;
		if (length > 0) {
			encoded |= 128;
		}
		output.push_back(encoded);
	} while (length > 0);
}

std::string MqttStatusService::trimCopy(const std::string& value) {
	size_t begin = 0;
	while (begin < value.size() &&
			std::isspace(static_cast<unsigned char>(value[begin]))) {
		++begin;
	}
	size_t end = value.size();
	while (end > begin &&
			std::isspace(static_cast<unsigned char>(value[end - 1]))) {
		--end;
	}
	return value.substr(begin, end - begin);
}

std::string MqttStatusService::lowerCopy(const std::string& value) {
	std::string result = value;
	std::transform(result.begin(), result.end(), result.begin(),
		[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
	return result;
}

unsigned int MqttStatusService::nowSeconds() {
	timeval tv;
	gettimeofday(&tv, nullptr);
	return static_cast<unsigned int>(tv.tv_sec);
}
