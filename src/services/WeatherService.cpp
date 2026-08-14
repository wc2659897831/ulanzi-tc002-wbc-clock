#include "services/WeatherService.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <netdb.h>
#include <unistd.h>

#include <base/base.h>
#include <base/json_array.h>
#include <base/json_object.h>

namespace {

static const double DEFAULT_LATITUDE = 23.0207;
static const double DEFAULT_LONGITUDE = 113.7518;
static const char* DEFAULT_TIMEZONE = "Asia/Shanghai";
static const char* DEFAULT_HTTP_WEATHER_HOST = "t.weather.itboy.net";
static const char* DEFAULT_HTTP_WEATHER_CITY_CODE = "101281601";
static const int HTTP_TIMEOUT_SEC = 4;
static const size_t HTTP_MAX_RESPONSE = 96 * 1024;

long currentTimeMs() {
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	return static_cast<long>(tv.tv_sec) * 1000L +
		static_cast<long>(tv.tv_usec) / 1000L;
}

} // namespace

WeatherService::WeatherService()
	: mRefreshing(false),
	  mStopRequested(false) {
	mSnapshot.hasWeather = false;
	mSnapshot.hasAir = false;
	mSnapshot.refreshing = false;
	mSnapshot.lastRequestFailed = false;
	mSnapshot.updatedAtMs = 0;
	mSnapshot.status = "WAIT";
	mSnapshot.timezone = DEFAULT_TIMEZONE;
	mSnapshot.latitude = DEFAULT_LATITUDE;
	mSnapshot.longitude = DEFAULT_LONGITUDE;
	mSnapshot.temperatureC = 0;
	mSnapshot.apparentTemperatureC = 0;
	mSnapshot.humidityPercent = 0;
	mSnapshot.pressureHPa = 0;
	mSnapshot.windSpeedKmh = 0;
	mSnapshot.windDirectionDeg = 0;
	mSnapshot.weatherCode = 0;
	mSnapshot.tempMaxC = 0;
	mSnapshot.tempMinC = 0;
	mSnapshot.precipitationPercent = 0;
	mSnapshot.uvIndex = 0;
	mSnapshot.aqi = 0;
	mSnapshot.pm25 = 0;
	mSnapshot.pm10 = 0;
}

WeatherService::~WeatherService() {
	stop();
}

void WeatherService::requestRefresh(bool force) {
	if (!force && !needsRefresh()) {
		return;
	}
	if (mRefreshing.load()) {
		return;
	}
	if (mThread.joinable()) {
		mThread.join();
	}

	mStopRequested = false;
	mRefreshing = true;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mSnapshot.refreshing = true;
		mSnapshot.status = mSnapshot.hasWeather ? "UPD" : "LOAD";
	}
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mSnapshot.status = "SPWN";
	}
	mThread = std::thread(&WeatherService::refreshRoutine, this);
}

void WeatherService::stop() {
	mStopRequested = true;
	if (mThread.joinable()) {
		mThread.join();
	}
	mRefreshing = false;
	std::lock_guard<std::mutex> lock(mMutex);
	mSnapshot.refreshing = false;
}

WeatherService::Snapshot WeatherService::snapshot() const {
	std::lock_guard<std::mutex> lock(mMutex);
	return mSnapshot;
}

bool WeatherService::needsRefresh() const {
	std::lock_guard<std::mutex> lock(mMutex);
	if (mSnapshot.refreshing) {
		return false;
	}
	if (!mSnapshot.hasWeather) {
		return !mSnapshot.lastRequestFailed;
	}
	return nowMs() - mSnapshot.updatedAtMs >= kRefreshIntervalMs;
}

long WeatherService::nowMs() {
	return currentTimeMs();
}

int WeatherService::roundToInt(double value) {
	return static_cast<int>(value >= 0 ? value + 0.5 : value - 0.5);
}

std::string WeatherService::urlEncode(const std::string& value) {
	std::ostringstream out;
	static const char* hex = "0123456789ABCDEF";
	for (size_t i = 0; i < value.size(); ++i) {
		const unsigned char ch = static_cast<unsigned char>(value[i]);
		if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
				(ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
				ch == '.') {
			out << static_cast<char>(ch);
		} else {
			out << '%' << hex[ch >> 4] << hex[ch & 0x0f];
		}
	}
	return out.str();
}

std::string WeatherService::lowerCopy(const std::string& value) {
	std::string result = value;
	std::transform(result.begin(), result.end(), result.begin(),
		[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
	return result;
}

std::string WeatherService::trimCopy(const std::string& value) {
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

int WeatherService::stringToInt(const std::string& value, int defaultValue) {
	if (value.empty()) {
		return defaultValue;
	}
	char* endPtr = nullptr;
	const long parsed = std::strtol(value.c_str(), &endPtr, 10);
	if (endPtr == value.c_str()) {
		return defaultValue;
	}
	return static_cast<int>(parsed);
}

int WeatherService::firstIntFromText(const std::string& value,
		int defaultValue) {
	for (size_t i = 0; i < value.size(); ++i) {
		const unsigned char ch = static_cast<unsigned char>(value[i]);
		if ((ch >= '0' && ch <= '9') || ch == '-') {
			char* endPtr = nullptr;
			const long parsed = std::strtol(value.c_str() + i, &endPtr, 10);
			if (endPtr != value.c_str() + i) {
				return static_cast<int>(parsed);
			}
		}
	}
	return defaultValue;
}

bool WeatherService::httpGet(const std::string& host, const std::string& path,
		std::string& body, std::string& error) {
	body.clear();
	error.clear();

	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	addrinfo* result = nullptr;
	const int gai = getaddrinfo(host.c_str(), "80", &hints, &result);
	if (gai != 0 || result == nullptr) {
		error = "DNS";
		return false;
	}

	int fd = -1;
	for (addrinfo* it = result; it != nullptr; it = it->ai_next) {
		fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
		if (fd < 0) {
			continue;
		}
		timeval timeout;
		timeout.tv_sec = HTTP_TIMEOUT_SEC;
		timeout.tv_usec = 0;
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
		if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
			break;
		}
		close(fd);
		fd = -1;
	}
	freeaddrinfo(result);

	if (fd < 0) {
		error = "CONN";
		return false;
	}

	std::ostringstream request;
	request << "GET " << path << " HTTP/1.1\r\n"
		<< "Host: " << host << "\r\n"
		<< "User-Agent: TC002-Weather/1.0\r\n"
		<< "Accept: application/json\r\n"
		<< "Connection: close\r\n\r\n";
	const std::string serialized = request.str();
	size_t sent = 0;
	while (sent < serialized.size()) {
		const ssize_t count = send(fd, serialized.data() + sent,
			serialized.size() - sent, 0);
		if (count <= 0) {
			close(fd);
			error = "SEND";
			return false;
		}
		sent += static_cast<size_t>(count);
	}

	std::string response;
	char buffer[2048];
	while (response.size() < HTTP_MAX_RESPONSE) {
		const ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
		if (count < 0) {
			close(fd);
			error = "RECV";
			return false;
		}
		if (count == 0) {
			break;
		}
		response.append(buffer, static_cast<size_t>(count));
	}
	close(fd);

	const size_t headerEnd = response.find("\r\n\r\n");
	if (headerEnd == std::string::npos) {
		error = "HTTP";
		return false;
	}

	const std::string header = response.substr(0, headerEnd);
	std::istringstream statusLine(header.substr(0, header.find("\r\n")));
	std::string httpVersion;
	int status = 0;
	statusLine >> httpVersion >> status;
	if (status != 200) {
		error = "H" + std::to_string(status);
		return false;
	}

	body = response.substr(headerEnd + 4);
	const std::string lowerHeader = lowerCopy(header);
	if (lowerHeader.find("transfer-encoding: chunked") != std::string::npos) {
		std::string decoded;
		if (!decodeChunkedBody(body, decoded)) {
			error = "CHNK";
			return false;
		}
		body = decoded;
	}
	return true;
}

bool WeatherService::decodeChunkedBody(const std::string& encoded,
		std::string& decoded) {
	decoded.clear();
	size_t offset = 0;
	while (offset < encoded.size()) {
		const size_t lineEnd = encoded.find("\r\n", offset);
		if (lineEnd == std::string::npos) {
			return false;
		}
		const std::string sizeText = trimCopy(encoded.substr(offset,
			lineEnd - offset));
		char* endPtr = nullptr;
		const long chunkSize = std::strtol(sizeText.c_str(), &endPtr, 16);
		if (endPtr == sizeText.c_str() || chunkSize < 0) {
			return false;
		}
		offset = lineEnd + 2;
		if (chunkSize == 0) {
			return true;
		}
		if (offset + static_cast<size_t>(chunkSize) > encoded.size()) {
			return false;
		}
		decoded.append(encoded, offset, static_cast<size_t>(chunkSize));
		offset += static_cast<size_t>(chunkSize);
		if (offset + 2 > encoded.size() || encoded.substr(offset, 2) != "\r\n") {
			return false;
		}
		offset += 2;
	}
	return true;
}

WeatherService::Location WeatherService::loadLocation() const {
	Location location;
	location.latitude = DEFAULT_LATITUDE;
	location.longitude = DEFAULT_LONGITUDE;
	location.timezone = DEFAULT_TIMEZONE;
	location.httpWeatherHost = DEFAULT_HTTP_WEATHER_HOST;
	location.httpWeatherCityCode = DEFAULT_HTTP_WEATHER_CITY_CODE;
	return location;
}

void WeatherService::refreshRoutine() {
	Snapshot next;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mSnapshot.status = "RUN";
		next = mSnapshot;
	}
	const Location location = loadLocation();
	next.latitude = location.latitude;
	next.longitude = location.longitude;
	next.timezone = location.timezone;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mSnapshot.status = "CFG";
	}

	std::string error;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mSnapshot.status = "HTTP";
	}
	const bool weatherOk = fetchHttpWeather(location, next, error);

	if (!mStopRequested.load()) {
		std::lock_guard<std::mutex> lock(mMutex);
		if (weatherOk) {
			next.updatedAtMs = nowMs();
			next.refreshing = false;
			next.lastRequestFailed = false;
			next.status = "OK";
			mSnapshot = next;
		} else {
			mSnapshot.refreshing = false;
			mSnapshot.lastRequestFailed = true;
			mSnapshot.status = error.empty() ? "ERR" : error;
		}
	}
	mRefreshing = false;
}

int WeatherService::mapWeatherTextToWeatherCode(const std::string& text) {
	if (text.find("雷") != std::string::npos) {
		return 95;
	}
	if (text.find("雨") != std::string::npos) {
		return 61;
	}
	if (text.find("雪") != std::string::npos) {
		return 71;
	}
	if (text.find("雾") != std::string::npos ||
			text.find("霾") != std::string::npos) {
		return 45;
	}
	if (text.find("阴") != std::string::npos ||
			text.find("云") != std::string::npos) {
		return 2;
	}
	return 0;
}

bool WeatherService::fetchHttpWeather(const Location& location,
		Snapshot& output, std::string& error) const {
	if (location.httpWeatherHost.empty() || location.httpWeatherCityCode.empty()) {
		error = "CFG";
		return false;
	}

	std::ostringstream path;
	path << "/api/weather/city/" << urlEncode(location.httpWeatherCityCode);

	std::string body;
	if (!httpGet(location.httpWeatherHost, path.str(), body, error)) {
		return false;
	}

	try {
		const base::JSONObject root = base::JSONObject::parse(body);
		if (root.getInt("status", 0) != 200) {
			error = "H" + std::to_string(root.getInt("status", 0));
			return false;
		}

		const base::JSONObject data = root.getObject("data");
		output.temperatureC = stringToInt(data.getString("wendu", ""), 0);
		output.apparentTemperatureC = output.temperatureC;
		output.humidityPercent = stringToInt(data.getString("shidu", ""), 0);
		output.pressureHPa = 0;
		output.windSpeedKmh = 0;
		output.windDirectionDeg = 0;
		output.weatherCode = 0;
		output.aqi = 0;
		output.hasWeather = true;

		const base::JSONArray forecast = data.getArray("forecast");
		if (forecast.size() > 0) {
			const base::JSONObject today = forecast.getObject(0);
			output.tempMaxC = firstIntFromText(today.getString("high", ""), 0);
			output.tempMinC = firstIntFromText(today.getString("low", ""), 0);
			output.precipitationPercent = 0;
			output.windSpeedKmh = firstIntFromText(today.getString("fl", ""), 0);
			output.weatherCode = mapWeatherTextToWeatherCode(
				today.getString("type", ""));
			output.aqi = today.getInt("aqi", 0);
			output.uvIndex = 0;
		}

		output.pm25 = roundToInt(data.getDouble("pm25", 0.0));
		output.pm10 = roundToInt(data.getDouble("pm10", 0.0));
		if (output.aqi <= 0) {
			output.aqi = output.pm25;
		}
		output.hasAir = true;
		return true;
	} catch (...) {
		error = "HJS";
		return false;
	}
}
