#ifndef SERVICES_WEATHER_SERVICE_H_
#define SERVICES_WEATHER_SERVICE_H_

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class WeatherService {
public:
	struct Snapshot {
		bool hasWeather;
		bool hasAir;
		bool refreshing;
		bool lastRequestFailed;
		long updatedAtMs;
		std::string status;
		std::string timezone;
		double latitude;
		double longitude;
		int temperatureC;
		int apparentTemperatureC;
		int humidityPercent;
		int pressureHPa;
		int windSpeedKmh;
		int windDirectionDeg;
		int weatherCode;
		int tempMaxC;
		int tempMinC;
		int precipitationPercent;
		int uvIndex;
		int aqi;
		int pm25;
		int pm10;
	};

	WeatherService();
	~WeatherService();

	void requestRefresh(bool force);
	void stop();
	Snapshot snapshot() const;
	bool needsRefresh() const;

private:
	struct Location {
		double latitude;
		double longitude;
		std::string timezone;
		std::string httpWeatherHost;
		std::string httpWeatherCityCode;
	};

	static const long kRefreshIntervalMs = 20L * 60L * 1000L;

	mutable std::mutex mMutex;
	Snapshot mSnapshot;
	std::atomic<bool> mRefreshing;
	std::atomic<bool> mStopRequested;
	std::thread mThread;

	static long nowMs();
	static int roundToInt(double value);
	static std::string urlEncode(const std::string& value);
	static std::string lowerCopy(const std::string& value);
	static std::string trimCopy(const std::string& value);
	static int stringToInt(const std::string& value, int defaultValue);
	static int firstIntFromText(const std::string& value, int defaultValue);
	static bool httpGet(const std::string& host, const std::string& path,
			std::string& body, std::string& error);
	static bool decodeChunkedBody(const std::string& encoded,
			std::string& decoded);

	Location loadLocation() const;
	void refreshRoutine();
	bool fetchHttpWeather(const Location& location, Snapshot& output,
			std::string& error) const;
	static int mapWeatherTextToWeatherCode(const std::string& text);
};

#endif
