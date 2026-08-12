#include "services/OfficialCompatServer.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <map>
#include <sstream>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <base/base.h>
#include <base/base64.h>
#include <base/json_object.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}
#include <net/NetManager.h>
#include <utils/BrightnessHelper.h>
#include <zlib.h>

#include "managers/AudioManager.h"
#include "pages/PageBase.h"

#define WIFIMANAGER NETMANAGER->getWifiManager()

namespace {

struct MemoryReader {
    const unsigned char* data;
    size_t size;
    size_t position;
};

int readMemory(void* opaque, unsigned char* buffer, int bufferSize) {
    MemoryReader* reader = static_cast<MemoryReader*>(opaque);
    if (reader->position >= reader->size) {
        return AVERROR_EOF;
    }
    const size_t remaining = reader->size - reader->position;
    const size_t count = std::min(remaining, static_cast<size_t>(bufferSize));
    memcpy(buffer, reader->data + reader->position, count);
    reader->position += count;
    return static_cast<int>(count);
}

int64_t seekMemory(void* opaque, int64_t offset, int whence) {
    MemoryReader* reader = static_cast<MemoryReader*>(opaque);
    if (whence == AVSEEK_SIZE) {
        return static_cast<int64_t>(reader->size);
    }

    int64_t next = 0;
    switch (whence) {
        case SEEK_SET:
            next = offset;
            break;
        case SEEK_CUR:
            next = static_cast<int64_t>(reader->position) + offset;
            break;
        case SEEK_END:
            next = static_cast<int64_t>(reader->size) + offset;
            break;
        default:
            return -1;
    }
    if (next < 0 || next > static_cast<int64_t>(reader->size)) {
        return -1;
    }
    reader->position = static_cast<size_t>(next);
    return next;
}

std::string lowerCopy(const std::string& value) {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return result;
}

std::string trimCopy(const std::string& value) {
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

int clampInt(int value, int minimum, int maximum) {
    return std::max(minimum, std::min(maximum, value));
}

bool looksLikePng(const std::vector<unsigned char>& data) {
    static const unsigned char signature[] =
            {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    return data.size() >= sizeof(signature) &&
            memcmp(data.data(), signature, sizeof(signature)) == 0;
}

bool looksLikeGif(const std::vector<unsigned char>& data) {
    return data.size() >= 6 &&
            (memcmp(data.data(), "GIF87a", 6) == 0 ||
             memcmp(data.data(), "GIF89a", 6) == 0);
}

bool looksLikeJpeg(const std::vector<unsigned char>& data) {
    return data.size() >= 3 && data[0] == 0xff &&
            data[1] == 0xd8 && data[2] == 0xff;
}

uint32_t readBigEndian32(const std::vector<unsigned char>& data, size_t offset) {
    return (static_cast<uint32_t>(data[offset]) << 24) |
            (static_cast<uint32_t>(data[offset + 1]) << 16) |
            (static_cast<uint32_t>(data[offset + 2]) << 8) |
            static_cast<uint32_t>(data[offset + 3]);
}

int pngBytesPerPixel(int colorType, int bitDepth) {
    if (bitDepth != 8) {
        return 0;
    }
    switch (colorType) {
        case 0: return 1;
        case 2: return 3;
        case 3: return 1;
        case 4: return 2;
        case 6: return 4;
        default: return 0;
    }
}

unsigned char paethPredictor(unsigned char a, unsigned char b,
        unsigned char c) {
    const int p = static_cast<int>(a) + static_cast<int>(b) -
            static_cast<int>(c);
    const int pa = std::abs(p - static_cast<int>(a));
    const int pb = std::abs(p - static_cast<int>(b));
    const int pc = std::abs(p - static_cast<int>(c));
    if (pa <= pb && pa <= pc) {
        return a;
    }
    if (pb <= pc) {
        return b;
    }
    return c;
}

uint16_t readLittleEndian16(const std::vector<unsigned char>& data,
        size_t offset) {
    return static_cast<uint16_t>(data[offset]) |
            (static_cast<uint16_t>(data[offset + 1]) << 8);
}

bool readGifSubBlocks(const std::vector<unsigned char>& data, size_t& offset,
        std::vector<unsigned char>& output) {
    output.clear();
    while (offset < data.size()) {
        const int size = data[offset++];
        if (size == 0) {
            return true;
        }
        if (offset + static_cast<size_t>(size) > data.size()) {
            return false;
        }
        output.insert(output.end(), data.begin() + offset,
                data.begin() + offset + size);
        offset += static_cast<size_t>(size);
    }
    return false;
}

bool skipGifSubBlocks(const std::vector<unsigned char>& data, size_t& offset) {
    std::vector<unsigned char> ignored;
    return readGifSubBlocks(data, offset, ignored);
}

bool gifReadCode(const std::vector<unsigned char>& data, int& bitPosition,
        int codeSize, int& code) {
    code = 0;
    for (int bit = 0; bit < codeSize; ++bit) {
        const size_t byteIndex = static_cast<size_t>(bitPosition / 8);
        if (byteIndex >= data.size()) {
            return false;
        }
        if (data[byteIndex] & (1 << (bitPosition % 8))) {
            code |= (1 << bit);
        }
        ++bitPosition;
    }
    return true;
}

bool decodeGifLzw(const std::vector<unsigned char>& compressed,
        int minCodeSize, size_t expectedPixels,
        std::vector<unsigned char>& indices) {
    if (minCodeSize < 2 || minCodeSize > 8) {
        return false;
    }

    const int clearCode = 1 << minCodeSize;
    const int endCode = clearCode + 1;
    int nextCode = endCode + 1;
    int codeSize = minCodeSize + 1;
    int bitPosition = 0;
    std::vector<std::vector<unsigned char>> table(4096);

    auto resetTable = [&]() {
        table.assign(4096, std::vector<unsigned char>());
        for (int i = 0; i < clearCode; ++i) {
            table[i].push_back(static_cast<unsigned char>(i));
        }
        nextCode = endCode + 1;
        codeSize = minCodeSize + 1;
    };

    resetTable();
    indices.clear();
    std::vector<unsigned char> previous;

    for (;;) {
        int code = 0;
        if (!gifReadCode(compressed, bitPosition, codeSize, code)) {
            break;
        }
        if (code == clearCode) {
            resetTable();
            previous.clear();
            continue;
        }
        if (code == endCode) {
            break;
        }

        std::vector<unsigned char> entry;
        if (code < static_cast<int>(table.size()) && !table[code].empty()) {
            entry = table[code];
        } else if (code == nextCode && !previous.empty()) {
            entry = previous;
            entry.push_back(previous.front());
        } else {
            return false;
        }

        indices.insert(indices.end(), entry.begin(), entry.end());
        if (!previous.empty() && nextCode < 4096) {
            std::vector<unsigned char> next = previous;
            next.push_back(entry.front());
            table[nextCode++] = next;
            if (nextCode == (1 << codeSize) && codeSize < 12) {
                ++codeSize;
            }
        }
        previous = entry;
        if (indices.size() >= expectedPixels) {
            indices.resize(expectedPixels);
            return true;
        }
    }
    return indices.size() >= expectedPixels;
}

}  // namespace

OfficialCompatServer& OfficialCompatServer::getInstance() {
    static OfficialCompatServer instance;
    return instance;
}

OfficialCompatServer::OfficialCompatServer()
    : mRunning(false),
      mListenFd(-1),
      mAnimationRunning(false),
      mAnimationStopRequested(false),
      mConfig{80, 50, 80, 100, 3, 60, 7, "UTC+8", "MM/DD", true, 1, false} {
}

OfficialCompatServer::~OfficialCompatServer() {
    stop();
}

void OfficialCompatServer::start() {
    if (mRunning.exchange(true)) {
        return;
    }
    av_register_all();
    avcodec_register_all();
    mServerThread = std::thread(&OfficialCompatServer::serverLoop, this);
    LOGI_TRACE("OfficialCompatServer: listening on 0.0.0.0:%d", kPort);
}

void OfficialCompatServer::stop() {
    if (!mRunning.exchange(false)) {
        stopAnimation();
        return;
    }

    if (mListenFd >= 0) {
        shutdown(mListenFd, SHUT_RDWR);
        close(mListenFd);
        mListenFd = -1;
    }
    if (mServerThread.joinable()) {
        mServerThread.join();
    }
    stopAnimation();
}

void OfficialCompatServer::serverLoop() {
    mListenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (mListenFd < 0) {
        LOGE_TRACE("OfficialCompatServer: socket failed: %s", strerror(errno));
        mRunning = false;
        return;
    }

    int reuse = 1;
    setsockopt(mListenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(kPort);
    if (bind(mListenFd, reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) < 0) {
        LOGE_TRACE("OfficialCompatServer: bind port %d failed: %s",
                kPort, strerror(errno));
        close(mListenFd);
        mListenFd = -1;
        mRunning = false;
        return;
    }
    if (listen(mListenFd, 4) < 0) {
        LOGE_TRACE("OfficialCompatServer: listen failed: %s", strerror(errno));
        close(mListenFd);
        mListenFd = -1;
        mRunning = false;
        return;
    }

    while (mRunning) {
        const int clientFd = accept(mListenFd, nullptr, nullptr);
        if (clientFd < 0) {
            if (mRunning) {
                LOGW_TRACE("OfficialCompatServer: accept failed: %s",
                        strerror(errno));
            }
            continue;
        }
        handleClient(clientFd);
        shutdown(clientFd, SHUT_RDWR);
        close(clientFd);
    }
}

void OfficialCompatServer::handleClient(int clientFd) {
    std::string request;
    char buffer[4096];
    size_t headerEnd = std::string::npos;
    while (request.size() < 16 * 1024) {
        const ssize_t received = recv(clientFd, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            return;
        }
        request.append(buffer, static_cast<size_t>(received));
        headerEnd = request.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            break;
        }
    }
    if (headerEnd == std::string::npos) {
        sendResponse(clientFd, 413, "text/plain", "request too large");
        return;
    }

    const std::string header = request.substr(0, headerEnd);
    const size_t firstLineEnd = header.find("\r\n");
    if (firstLineEnd == std::string::npos) {
        sendResponse(clientFd, 400, "text/plain", "bad request");
        return;
    }

    std::istringstream requestLine(header.substr(0, firstLineEnd));
    std::string method;
    std::string target;
    std::string version;
    requestLine >> method >> target >> version;
    if (method.empty() || target.empty()) {
        sendResponse(clientFd, 400, "text/plain", "bad request");
        return;
    }

    std::map<std::string, std::string> headers;
    size_t lineBegin = firstLineEnd + 2;
    while (lineBegin < header.size()) {
        const size_t lineEnd = header.find("\r\n", lineBegin);
        const std::string line = header.substr(lineBegin,
                lineEnd == std::string::npos ? std::string::npos :
                lineEnd - lineBegin);
        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            headers[lowerCopy(trimCopy(line.substr(0, colon)))] =
                    trimCopy(line.substr(colon + 1));
        }
        if (lineEnd == std::string::npos) {
            break;
        }
        lineBegin = lineEnd + 2;
    }

    if (method == "OPTIONS") {
        sendResponse(clientFd, 200, "text/plain", "ok");
        return;
    }

    std::string body = request.substr(headerEnd + 4);
    const auto transferEncodingIt = headers.find("transfer-encoding");
    if (transferEncodingIt != headers.end() &&
            lowerCopy(transferEncodingIt->second).find("chunked") !=
            std::string::npos) {
        if (!readChunkedBody(clientFd, body, body)) {
            sendResponse(clientFd, 400, "text/plain", "bad chunked body");
            return;
        }
    } else {
        size_t contentLength = 0;
        const auto contentLengthIt = headers.find("content-length");
        if (contentLengthIt != headers.end()) {
            try {
                contentLength = static_cast<size_t>(
                        std::stoul(contentLengthIt->second));
            } catch (...) {
                sendResponse(clientFd, 400, "text/plain", "bad content length");
                return;
            }
        }
        if (contentLength > kMaxRequestBody) {
            sendResponse(clientFd, 413, "text/plain", "body too large");
            return;
        }

        while (body.size() < contentLength) {
            const size_t remaining = contentLength - body.size();
            const ssize_t received = recv(clientFd, buffer,
                    std::min(sizeof(buffer), remaining), 0);
            if (received <= 0) {
                sendResponse(clientFd, 400, "text/plain", "incomplete body");
                return;
            }
            body.append(buffer, static_cast<size_t>(received));
        }
        if (body.size() > contentLength) {
            body.resize(contentLength);
        }
    }

    const size_t queryBegin = target.find('?');
    const std::string path = target.substr(0, queryBegin);
    if (method == "GET" && path == "/getBase") {
        sendResponse(clientFd, 200, "application/json", baseJson());
    } else if (method == "GET" && path == "/getConfig") {
        sendResponse(clientFd, 200, "application/json", configJson());
    } else if (method == "POST" && path == "/setConfig") {
        try {
            setConfig(body);
            sendResponse(clientFd, 200, "application/json",
                    "{\"code\":200,\"message\":\"success\"}");
        } catch (...) {
            sendResponse(clientFd, 400, "application/json",
                    "{\"code\":400,\"message\":\"invalid config\"}");
        }
    } else if (method == "POST" && path == "/api/custom") {
        handleCustom(clientFd, target, body);
    } else {
        sendResponse(clientFd, 404, "text/plain", "not found");
    }
}

void OfficialCompatServer::handleCustom(int clientFd,
        const std::string& target, const std::string& body) {
    try {
        const base::JSONObject request = base::JSONObject::parse(body.empty() ?
                "{}" : body);
        const std::string appName = extractQueryValue(target, "name");
        LOGI_TRACE("OfficialCompatServer: custom app=%s", appName.c_str());

        if (request.empty()) {
            clearDisplay();
            sendResponse(clientFd, 200, "application/json",
                    "{\"code\":200,\"message\":\"success\"}");
            return;
        }

        const base::JSONArray images = request.getArray("image");
        if (images.size() <= 0) {
            sendResponse(clientFd, 200, "application/json",
                    "{\"code\":200,\"message\":\"success\"}");
            return;
        }

        const base::JSONObject image = images.getObject(0);
        const std::string dataUri = image.getString("data", "");
        if (dataUri.empty()) {
            sendResponse(clientFd, 400, "application/json",
                    "{\"code\":400,\"message\":\"missing image data\"}");
            return;
        }

        int positionX = 0;
        int positionY = 0;
        const base::JSONArray position = image.getArray("position");
        if (position.size() >= 2) {
            positionX = position.getInt(0);
            positionY = position.getInt(1);
        }

        std::vector<DecodedFrame> frames;
        if (!decodeImageDataUri(dataUri, frames) || frames.empty()) {
            showDecodeError();
            sendResponse(clientFd, 200, "application/json",
                    "{\"code\":200,\"message\":\"accepted unsupported image data\"}");
            return;
        }

        const int durationSeconds = request.getInt("duration", 0);
        displayFrames(frames, positionX, positionY, durationSeconds);
        sendResponse(clientFd, 200, "application/json",
                "{\"code\":200,\"message\":\"success\"}");
    } catch (...) {
        sendResponse(clientFd, 400, "application/json",
                "{\"code\":400,\"message\":\"invalid request\"}");
    }
}

std::string OfficialCompatServer::baseJson() const {
    std::string ssid;
    std::string ip = "0.0.0.0";
    std::string mac = base::wifiMacAddress();
    if (WIFIMANAGER != nullptr) {
        ip = WIFIMANAGER->getIp() ? WIFIMANAGER->getIp() : "0.0.0.0";
        mac = WIFIMANAGER->getMacAddr() ? WIFIMANAGER->getMacAddr() : mac;
        WifiInfo* info = WIFIMANAGER->getConnectionInfo();
        if (info != nullptr) {
            ssid = info->getSsid();
        }
    }

    std::ostringstream json;
    json << "{\"devSn\":\"custom-tc002\""
         << ",\"ssid\":\"" << jsonEscape(ssid) << "\""
         << ",\"ip\":\"" << jsonEscape(ip) << "\""
         << ",\"mac\":\"" << jsonEscape(mac) << "\""
         << ",\"mcuVer\":\"custom\""
         << ",\"appVer\":\"dev\"}";
    return json.str();
}

std::string OfficialCompatServer::configJson() const {
    std::lock_guard<std::mutex> lock(mConfigMutex);
    std::ostringstream json;
    const DeviceConfig& config = mConfig;
    std::string level = "mid";
    if (config.brightness <= config.lowBrightness) {
        level = "low";
    } else if (config.brightness >= config.highBrightness) {
        level = "high";
    }
    json << "{\"brightness\":{\"level\":\"" << level
         << "\",\"low\":" << config.lowBrightness
         << ",\"mid\":" << config.midBrightness
         << ",\"high\":" << config.highBrightness << "}"
         << ",\"volume\":" << config.volume
         << ",\"carouselSpeed\":" << config.carouselSpeed
         << ",\"scrollSpeed\":" << config.scrollSpeed
         << ",\"timezone\":\"" << jsonEscape(config.timezone) << "\""
         << ",\"dateFormat\":\"" << jsonEscape(config.dateFormat) << "\""
         << ",\"showWeek\":" << (config.showWeek ? "true" : "false")
         << ",\"weekStart\":" << config.weekStart
         << ",\"lowBatteryAutoSleep\":"
         << (config.lowBatteryAutoSleep ? "true" : "false") << "}";
    return json.str();
}

void OfficialCompatServer::setConfig(const std::string& body) {
    const base::JSONObject request = base::JSONObject::parse(body.empty() ?
            "{}" : body);
    std::lock_guard<std::mutex> lock(mConfigMutex);

    if (request.has("brightness")) {
        const base::JSONObject brightness = request.getObject("brightness");
        if (brightness.has("low")) {
            mConfig.lowBrightness = clampInt(
                    brightness.getInt("low"), 0, 100);
        }
        if (brightness.has("mid")) {
            mConfig.midBrightness = clampInt(
                    brightness.getInt("mid"), 0, 100);
        }
        if (brightness.has("high")) {
            mConfig.highBrightness = clampInt(
                    brightness.getInt("high"), 0, 100);
        }
        if (brightness.has("level")) {
            const std::string level = brightness.getString("level");
            if (level == "low") {
                mConfig.brightness = mConfig.lowBrightness;
            } else if (level == "high") {
                mConfig.brightness = mConfig.highBrightness;
            } else {
                mConfig.brightness = mConfig.midBrightness;
            }
        }
        BRIGHTNESSHELPER->setBrightness(mConfig.brightness);
    }
    if (request.has("volume")) {
        mConfig.volume = clampInt(request.getInt("volume"), 0, 6);
        awtrix::AudioManager::getInstance().setVolume(mConfig.volume);
    }
    if (request.has("carouselSpeed")) {
        mConfig.carouselSpeed = request.getInt("carouselSpeed");
    }
    if (request.has("scrollSpeed")) {
        mConfig.scrollSpeed = request.getInt("scrollSpeed");
    }
    if (request.has("timezone")) {
        mConfig.timezone = request.getString("timezone");
    }
    if (request.has("dateFormat")) {
        mConfig.dateFormat = request.getString("dateFormat");
    }
    if (request.has("showWeek")) {
        mConfig.showWeek = request.getBool("showWeek");
    }
    if (request.has("weekStart")) {
        mConfig.weekStart = request.getInt("weekStart");
    }
    if (request.has("lowBatteryAutoSleep")) {
        mConfig.lowBatteryAutoSleep = request.getBool("lowBatteryAutoSleep");
    }
}

bool OfficialCompatServer::readChunkedBody(int clientFd, std::string& body,
        const std::string& initialBody) const {
    std::string stream = initialBody;
    body.clear();
    char buffer[4096];
    for (;;) {
        size_t lineEnd = stream.find("\r\n");
        while (lineEnd == std::string::npos) {
            const ssize_t received = recv(clientFd, buffer, sizeof(buffer), 0);
            if (received <= 0) {
                return false;
            }
            stream.append(buffer, static_cast<size_t>(received));
            lineEnd = stream.find("\r\n");
        }

        const std::string sizeText = trimCopy(stream.substr(0, lineEnd));
        char* endPtr = nullptr;
        const long chunkSize = std::strtol(sizeText.c_str(), &endPtr, 16);
        if (endPtr == sizeText.c_str() || chunkSize < 0) {
            return false;
        }
        stream.erase(0, lineEnd + 2);
        if (chunkSize == 0) {
            return true;
        }
        if (body.size() + static_cast<size_t>(chunkSize) > kMaxRequestBody) {
            return false;
        }

        while (stream.size() < static_cast<size_t>(chunkSize) + 2) {
            const ssize_t received = recv(clientFd, buffer, sizeof(buffer), 0);
            if (received <= 0) {
                return false;
            }
            stream.append(buffer, static_cast<size_t>(received));
        }
        body.append(stream.data(), static_cast<size_t>(chunkSize));
        stream.erase(0, static_cast<size_t>(chunkSize) + 2);
    }
}

bool OfficialCompatServer::decodeImageDataUri(const std::string& dataUri,
        std::vector<DecodedFrame>& frames) const {
    std::string mime;
    std::string payload = dataUri;
    const size_t comma = dataUri.find(',');
    if (comma != std::string::npos) {
        const std::string metadata = lowerCopy(dataUri.substr(0, comma));
        payload = dataUri.substr(comma + 1);
        if (metadata.find("image/png") != std::string::npos) {
            mime = "image/png";
        } else if (metadata.find("image/gif") != std::string::npos) {
            mime = "image/gif";
        } else if (metadata.find("image/jpeg") != std::string::npos ||
                metadata.find("image/jpg") != std::string::npos) {
            mime = "image/jpeg";
        }
    }

    std::string decoded;
    base::decodeBase64(stripBase64Whitespace(payload), &decoded);
    if (decoded.empty()) {
        LOGW_TRACE("OfficialCompatServer: base64 decode produced empty image");
        return false;
    }
    const std::vector<unsigned char> bytes(decoded.begin(), decoded.end());
    if (mime.empty()) {
        if (looksLikePng(bytes)) {
            mime = "image/png";
        } else if (looksLikeGif(bytes)) {
            mime = "image/gif";
        } else if (looksLikeJpeg(bytes)) {
            mime = "image/jpeg";
        }
    }
    LOGI_TRACE("OfficialCompatServer: image mime=%s bytes=%d",
            mime.c_str(), static_cast<int>(bytes.size()));
    return decodeImage(bytes, mime, frames);
}

bool OfficialCompatServer::decodeImage(
        const std::vector<unsigned char>& encoded, const std::string& mime,
        std::vector<DecodedFrame>& frames) const {
    if (mime == "image/png") {
        if (decodePngImage(encoded, frames)) {
            return true;
        }
        LOGW_TRACE("OfficialCompatServer: zlib PNG decode failed, trying ffmpeg");
        return decodeStillImage(encoded, AV_CODEC_ID_PNG, frames);
    }
    if (mime == "image/jpeg") {
        return decodeStillImage(encoded, AV_CODEC_ID_MJPEG, frames);
    }
    if (mime == "image/gif") {
        if (decodeGifImage(encoded, frames)) {
            return true;
        }
        LOGW_TRACE("OfficialCompatServer: built-in GIF decode failed, trying ffmpeg");
    } else {
        return false;
    }

    MemoryReader reader = {encoded.data(), encoded.size(), 0};
    const int ioBufferSize = 4096;
    unsigned char* ioBuffer = static_cast<unsigned char*>(
            av_malloc(ioBufferSize));
    if (ioBuffer == nullptr) {
        return false;
    }
    AVIOContext* io = avio_alloc_context(ioBuffer, ioBufferSize, 0, &reader,
            readMemory, nullptr, seekMemory);
    if (io == nullptr) {
        av_free(ioBuffer);
        return false;
    }

    AVFormatContext* format = avformat_alloc_context();
    if (format == nullptr) {
        av_freep(&io->buffer);
        avio_context_free(&io);
        return false;
    }
    format->pb = io;
    format->flags |= AVFMT_FLAG_CUSTOM_IO;

    if (avformat_open_input(&format, nullptr, nullptr, nullptr) < 0 ||
            avformat_find_stream_info(format, nullptr) < 0) {
        if (format != nullptr) {
            avformat_close_input(&format);
        }
        av_freep(&io->buffer);
        avio_context_free(&io);
        return false;
    }

    int videoStream = -1;
    for (unsigned int i = 0; i < format->nb_streams; ++i) {
        if (format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = static_cast<int>(i);
            break;
        }
    }
    if (videoStream < 0) {
        avformat_close_input(&format);
        av_freep(&io->buffer);
        avio_context_free(&io);
        return false;
    }

    AVCodecParameters* parameters = format->streams[videoStream]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(parameters->codec_id);
    AVCodecContext* codecContext = avcodec_alloc_context3(codec);
    if (codec == nullptr || codecContext == nullptr ||
            avcodec_parameters_to_context(codecContext, parameters) < 0 ||
            avcodec_open2(codecContext, codec, nullptr) < 0) {
        if (codecContext != nullptr) {
            avcodec_free_context(&codecContext);
        }
        avformat_close_input(&format);
        av_freep(&io->buffer);
        avio_context_free(&io);
        return false;
    }

    AVFrame* decodedFrame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    SwsContext* scaler = nullptr;
    int64_t previousPts = AV_NOPTS_VALUE;
    if (decodedFrame == nullptr || packet == nullptr) {
        av_frame_free(&decodedFrame);
        av_packet_free(&packet);
        avcodec_free_context(&codecContext);
        avformat_close_input(&format);
        av_freep(&io->buffer);
        avio_context_free(&io);
        return false;
    }

    auto appendFrame = [&](AVFrame* source, int packetDuration) -> bool {
        if (source->width <= 0 || source->height <= 0 ||
                source->width > 512 || source->height > 512) {
            return false;
        }
        if (scaler == nullptr) {
            scaler = sws_getContext(source->width, source->height,
                    static_cast<AVPixelFormat>(source->format),
                    source->width, source->height, AV_PIX_FMT_RGBA,
                    SWS_BILINEAR, nullptr, nullptr, nullptr);
        }
        if (scaler == nullptr) {
            return false;
        }

        DecodedFrame output;
        output.width = source->width;
        output.height = source->height;
        output.delayMs = 100;
        const AVRational timeBase = format->streams[videoStream]->time_base;
        const int64_t pts = source->best_effort_timestamp;
        if (pts != AV_NOPTS_VALUE && previousPts != AV_NOPTS_VALUE &&
                pts > previousPts) {
            const double milliseconds =
                    (pts - previousPts) * av_q2d(timeBase) * 1000.0;
            output.delayMs = clampInt(static_cast<int>(milliseconds), 20, 5000);
        } else if (packetDuration > 0) {
            const double milliseconds =
                    packetDuration * av_q2d(timeBase) * 1000.0;
            output.delayMs = clampInt(static_cast<int>(milliseconds), 20, 5000);
        }
        previousPts = pts;

        std::vector<unsigned char> rgba(static_cast<size_t>(output.width) *
                static_cast<size_t>(output.height) * 4);
        uint8_t* destination[4] = {rgba.data(), nullptr, nullptr, nullptr};
        int destinationLinesize[4] = {output.width * 4, 0, 0, 0};
        if (sws_scale(scaler, source->data, source->linesize, 0,
                output.height, destination, destinationLinesize) <= 0) {
            return false;
        }
        output.rgb.resize(static_cast<size_t>(output.width) *
                static_cast<size_t>(output.height) * 3);
        for (size_t i = 0, j = 0; i + 3 < rgba.size(); i += 4, j += 3) {
            const int alpha = rgba[i + 3];
            output.rgb[j] = static_cast<unsigned char>(rgba[i] * alpha / 255);
            output.rgb[j + 1] = static_cast<unsigned char>(
                    rgba[i + 1] * alpha / 255);
            output.rgb[j + 2] = static_cast<unsigned char>(
                    rgba[i + 2] * alpha / 255);
        }
        frames.push_back(output);
        return true;
    };

    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == videoStream) {
            if (avcodec_send_packet(codecContext, packet) >= 0) {
                while (avcodec_receive_frame(codecContext, decodedFrame) >= 0) {
                    if (!appendFrame(decodedFrame, packet->duration)) {
                        av_packet_unref(packet);
                        sws_freeContext(scaler);
                        av_frame_free(&decodedFrame);
                        av_packet_free(&packet);
                        avcodec_free_context(&codecContext);
                        avformat_close_input(&format);
                        av_freep(&io->buffer);
                        avio_context_free(&io);
                        return false;
                    }
                }
            }
        }
        av_packet_unref(packet);
    }
    avcodec_send_packet(codecContext, nullptr);
    while (avcodec_receive_frame(codecContext, decodedFrame) >= 0) {
        if (!appendFrame(decodedFrame, 0)) {
            frames.clear();
            break;
        }
    }

    sws_freeContext(scaler);
    av_frame_free(&decodedFrame);
    av_packet_free(&packet);
    avcodec_free_context(&codecContext);
    avformat_close_input(&format);
    av_freep(&io->buffer);
    avio_context_free(&io);
    return !frames.empty();
}

bool OfficialCompatServer::decodeGifImage(
        const std::vector<unsigned char>& encoded,
        std::vector<DecodedFrame>& frames) const {
    if (!looksLikeGif(encoded) || encoded.size() < 13) {
        return false;
    }

    const int canvasWidth = readLittleEndian16(encoded, 6);
    const int canvasHeight = readLittleEndian16(encoded, 8);
    if (canvasWidth <= 0 || canvasHeight <= 0 ||
            canvasWidth > 512 || canvasHeight > 512) {
        return false;
    }

    const unsigned char packed = encoded[10];
    const bool hasGlobalPalette = (packed & 0x80) != 0;
    const int globalPaletteSize = hasGlobalPalette ?
            (1 << ((packed & 0x07) + 1)) : 0;
    const int backgroundIndex = encoded[11];
    size_t offset = 13;
    std::vector<unsigned char> globalPalette;
    if (hasGlobalPalette) {
        const size_t paletteBytes = static_cast<size_t>(globalPaletteSize) * 3;
        if (offset + paletteBytes > encoded.size()) {
            return false;
        }
        globalPalette.assign(encoded.begin() + offset,
                encoded.begin() + offset + paletteBytes);
        offset += paletteBytes;
    }

    std::vector<unsigned char> canvas(static_cast<size_t>(canvasWidth) *
            static_cast<size_t>(canvasHeight) * 3, 0);
    if (hasGlobalPalette && backgroundIndex >= 0 &&
            backgroundIndex < globalPaletteSize) {
        const size_t paletteOffset = static_cast<size_t>(backgroundIndex) * 3;
        for (int y = 0; y < canvasHeight; ++y) {
            for (int x = 0; x < canvasWidth; ++x) {
                const size_t destination = (static_cast<size_t>(y) *
                        canvasWidth + static_cast<size_t>(x)) * 3;
                canvas[destination] = globalPalette[paletteOffset];
                canvas[destination + 1] = globalPalette[paletteOffset + 1];
                canvas[destination + 2] = globalPalette[paletteOffset + 2];
            }
        }
    }

    int delayMs = 100;
    int dispose = 0;
    bool transparent = false;
    int transparentIndex = -1;

    while (offset < encoded.size()) {
        const unsigned char marker = encoded[offset++];
        if (marker == 0x3b) {
            break;
        }
        if (marker == 0x21) {
            if (offset >= encoded.size()) {
                return false;
            }
            const unsigned char label = encoded[offset++];
            if (label == 0xf9) {
                if (offset + 6 > encoded.size() || encoded[offset] != 4) {
                    return false;
                }
                ++offset;
                const unsigned char gcePacked = encoded[offset++];
                const int delayCs = readLittleEndian16(encoded, offset);
                offset += 2;
                transparentIndex = encoded[offset++];
                if (encoded[offset++] != 0) {
                    return false;
                }
                dispose = (gcePacked >> 2) & 0x07;
                transparent = (gcePacked & 0x01) != 0;
                delayMs = delayCs > 0 ? clampInt(delayCs * 10, 20, 5000) : 100;
            } else {
                if (!skipGifSubBlocks(encoded, offset)) {
                    return false;
                }
            }
            continue;
        }
        if (marker != 0x2c) {
            return false;
        }
        if (offset + 9 > encoded.size()) {
            return false;
        }

        const int left = readLittleEndian16(encoded, offset);
        const int top = readLittleEndian16(encoded, offset + 2);
        const int width = readLittleEndian16(encoded, offset + 4);
        const int height = readLittleEndian16(encoded, offset + 6);
        const unsigned char imagePacked = encoded[offset + 8];
        offset += 9;
        const bool hasLocalPalette = (imagePacked & 0x80) != 0;
        const bool interlace = (imagePacked & 0x40) != 0;
        const int localPaletteSize = hasLocalPalette ?
                (1 << ((imagePacked & 0x07) + 1)) : 0;
        std::vector<unsigned char> localPalette;
        if (hasLocalPalette) {
            const size_t paletteBytes = static_cast<size_t>(localPaletteSize) * 3;
            if (offset + paletteBytes > encoded.size()) {
                return false;
            }
            localPalette.assign(encoded.begin() + offset,
                    encoded.begin() + offset + paletteBytes);
            offset += paletteBytes;
        }
        const std::vector<unsigned char>& palette =
                hasLocalPalette ? localPalette : globalPalette;
        const int paletteSize = hasLocalPalette ?
                localPaletteSize : globalPaletteSize;
        if (palette.empty() || width <= 0 || height <= 0) {
            return false;
        }
        if (offset >= encoded.size()) {
            return false;
        }
        const int minCodeSize = encoded[offset++];
        std::vector<unsigned char> compressed;
        if (!readGifSubBlocks(encoded, offset, compressed)) {
            return false;
        }

        std::vector<unsigned char> indices;
        if (!decodeGifLzw(compressed, minCodeSize,
                static_cast<size_t>(width) * static_cast<size_t>(height),
                indices)) {
            return false;
        }

        std::vector<unsigned char> previousCanvas;
        if (dispose == 3) {
            previousCanvas = canvas;
        }

        auto paintPixel = [&](int sourceIndex, int x, int y) -> bool {
            if (x < 0 || y < 0 || x >= canvasWidth || y >= canvasHeight) {
                return true;
            }
            const int paletteIndex = indices[sourceIndex];
            if (transparent && paletteIndex == transparentIndex) {
                return true;
            }
            if (paletteIndex < 0 || paletteIndex >= paletteSize) {
                return false;
            }
            const size_t paletteOffset = static_cast<size_t>(paletteIndex) * 3;
            const size_t destination = (static_cast<size_t>(y) *
                    canvasWidth + static_cast<size_t>(x)) * 3;
            canvas[destination] = palette[paletteOffset];
            canvas[destination + 1] = palette[paletteOffset + 1];
            canvas[destination + 2] = palette[paletteOffset + 2];
            return true;
        };

        if (!interlace) {
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const int sourceIndex = y * width + x;
                    if (!paintPixel(sourceIndex, left + x, top + y)) {
                        return false;
                    }
                }
            }
        } else {
            static const int starts[] = {0, 4, 2, 1};
            static const int steps[] = {8, 8, 4, 2};
            int sourceIndex = 0;
            for (int pass = 0; pass < 4; ++pass) {
                for (int y = starts[pass]; y < height; y += steps[pass]) {
                    for (int x = 0; x < width; ++x) {
                        if (!paintPixel(sourceIndex++, left + x, top + y)) {
                            return false;
                        }
                    }
                }
            }
        }

        DecodedFrame frame;
        frame.width = canvasWidth;
        frame.height = canvasHeight;
        frame.delayMs = delayMs;
        frame.rgb = canvas;
        frames.push_back(frame);

        if (dispose == 2) {
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const int destinationX = left + x;
                    const int destinationY = top + y;
                    if (destinationX < 0 || destinationY < 0 ||
                            destinationX >= canvasWidth ||
                            destinationY >= canvasHeight) {
                        continue;
                    }
                    const size_t destination =
                            (static_cast<size_t>(destinationY) *
                             canvasWidth + static_cast<size_t>(destinationX)) * 3;
                    canvas[destination] = 0;
                    canvas[destination + 1] = 0;
                    canvas[destination + 2] = 0;
                }
            }
        } else if (dispose == 3 && !previousCanvas.empty()) {
            canvas = previousCanvas;
        }

        delayMs = 100;
        dispose = 0;
        transparent = false;
        transparentIndex = -1;
    }

    LOGI_TRACE("OfficialCompatServer: GIF decoded frames=%d w=%d h=%d",
            static_cast<int>(frames.size()), canvasWidth, canvasHeight);
    return !frames.empty();
}

bool OfficialCompatServer::decodePngImage(
        const std::vector<unsigned char>& encoded,
        std::vector<DecodedFrame>& frames) const {
    if (!looksLikePng(encoded)) {
        return false;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    int bitDepth = 0;
    int colorType = 0;
    int interlace = 0;
    std::vector<unsigned char> compressed;
    std::vector<unsigned char> palette;
    std::vector<unsigned char> transparency;

    size_t offset = 8;
    while (offset + 12 <= encoded.size()) {
        const uint32_t length = readBigEndian32(encoded, offset);
        if (offset + 12 + length > encoded.size()) {
            LOGW_TRACE("OfficialCompatServer: PNG chunk exceeds input");
            return false;
        }
        const char* type = reinterpret_cast<const char*>(&encoded[offset + 4]);
        const size_t dataOffset = offset + 8;
        if (memcmp(type, "IHDR", 4) == 0) {
            if (length != 13) {
                return false;
            }
            width = readBigEndian32(encoded, dataOffset);
            height = readBigEndian32(encoded, dataOffset + 4);
            bitDepth = encoded[dataOffset + 8];
            colorType = encoded[dataOffset + 9];
            interlace = encoded[dataOffset + 12];
        } else if (memcmp(type, "PLTE", 4) == 0) {
            palette.assign(encoded.begin() + dataOffset,
                    encoded.begin() + dataOffset + length);
        } else if (memcmp(type, "tRNS", 4) == 0) {
            transparency.assign(encoded.begin() + dataOffset,
                    encoded.begin() + dataOffset + length);
        } else if (memcmp(type, "IDAT", 4) == 0) {
            compressed.insert(compressed.end(), encoded.begin() + dataOffset,
                    encoded.begin() + dataOffset + length);
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        offset += 12 + length;
    }

    const int channels = pngBytesPerPixel(colorType, bitDepth);
    if (width == 0 || height == 0 || width > 512 || height > 512 ||
            channels <= 0 || interlace != 0 || compressed.empty()) {
        LOGW_TRACE("OfficialCompatServer: unsupported PNG w=%d h=%d bit=%d color=%d interlace=%d",
                static_cast<int>(width), static_cast<int>(height),
                bitDepth, colorType, interlace);
        return false;
    }
    if (colorType == 3 && palette.empty()) {
        return false;
    }

    const size_t rowBytes = static_cast<size_t>(width) *
            static_cast<size_t>(channels);
    const size_t inflatedSize = (rowBytes + 1) * static_cast<size_t>(height);
    std::vector<unsigned char> inflated(inflatedSize);
    uLongf destinationSize = static_cast<uLongf>(inflated.size());
    const int zResult = uncompress(inflated.data(), &destinationSize,
            compressed.data(), static_cast<uLong>(compressed.size()));
    if (zResult != Z_OK || destinationSize < inflatedSize) {
        LOGW_TRACE("OfficialCompatServer: PNG inflate failed result=%d out=%d expected=%d",
                zResult, static_cast<int>(destinationSize),
                static_cast<int>(inflatedSize));
        return false;
    }

    std::vector<unsigned char> raw(rowBytes * static_cast<size_t>(height), 0);
    size_t inputOffset = 0;
    for (uint32_t y = 0; y < height; ++y) {
        if (inputOffset + 1 + rowBytes > inflated.size()) {
            return false;
        }
        const int filter = inflated[inputOffset++];
        unsigned char* row = raw.data() + static_cast<size_t>(y) * rowBytes;
        const unsigned char* previous = y > 0 ? row - rowBytes : nullptr;
        for (size_t x = 0; x < rowBytes; ++x) {
            const unsigned char value = inflated[inputOffset++];
            const unsigned char left = x >= static_cast<size_t>(channels) ?
                    row[x - channels] : 0;
            const unsigned char up = previous != nullptr ? previous[x] : 0;
            const unsigned char upLeft = (previous != nullptr &&
                    x >= static_cast<size_t>(channels)) ?
                    previous[x - channels] : 0;
            switch (filter) {
                case 0:
                    row[x] = value;
                    break;
                case 1:
                    row[x] = static_cast<unsigned char>(value + left);
                    break;
                case 2:
                    row[x] = static_cast<unsigned char>(value + up);
                    break;
                case 3:
                    row[x] = static_cast<unsigned char>(value +
                            ((static_cast<int>(left) +
                              static_cast<int>(up)) / 2));
                    break;
                case 4:
                    row[x] = static_cast<unsigned char>(value +
                            paethPredictor(left, up, upLeft));
                    break;
                default:
                    LOGW_TRACE("OfficialCompatServer: unsupported PNG filter=%d",
                            filter);
                    return false;
            }
        }
    }

    DecodedFrame output;
    output.width = static_cast<int>(width);
    output.height = static_cast<int>(height);
    output.delayMs = 100;
    output.rgb.resize(static_cast<size_t>(output.width) *
            static_cast<size_t>(output.height) * 3, 0);
    for (uint32_t y = 0; y < height; ++y) {
        const unsigned char* row = raw.data() + static_cast<size_t>(y) *
                rowBytes;
        for (uint32_t x = 0; x < width; ++x) {
            unsigned char r = 0;
            unsigned char g = 0;
            unsigned char b = 0;
            unsigned char a = 255;
            const size_t source = static_cast<size_t>(x) *
                    static_cast<size_t>(channels);
            if (colorType == 0) {
                r = g = b = row[source];
            } else if (colorType == 2) {
                r = row[source];
                g = row[source + 1];
                b = row[source + 2];
            } else if (colorType == 3) {
                const int paletteIndex = row[source];
                const size_t paletteOffset = static_cast<size_t>(paletteIndex) * 3;
                if (paletteOffset + 2 >= palette.size()) {
                    return false;
                }
                r = palette[paletteOffset];
                g = palette[paletteOffset + 1];
                b = palette[paletteOffset + 2];
                if (static_cast<size_t>(paletteIndex) < transparency.size()) {
                    a = transparency[paletteIndex];
                }
            } else if (colorType == 4) {
                r = g = b = row[source];
                a = row[source + 1];
            } else if (colorType == 6) {
                r = row[source];
                g = row[source + 1];
                b = row[source + 2];
                a = row[source + 3];
            }
            const size_t destination = (static_cast<size_t>(y) *
                    static_cast<size_t>(width) + static_cast<size_t>(x)) * 3;
            output.rgb[destination] = static_cast<unsigned char>(
                    static_cast<int>(r) * static_cast<int>(a) / 255);
            output.rgb[destination + 1] = static_cast<unsigned char>(
                    static_cast<int>(g) * static_cast<int>(a) / 255);
            output.rgb[destination + 2] = static_cast<unsigned char>(
                    static_cast<int>(b) * static_cast<int>(a) / 255);
        }
    }

    frames.push_back(output);
    LOGI_TRACE("OfficialCompatServer: PNG decoded w=%d h=%d color=%d",
            output.width, output.height, colorType);
    return true;
}

bool OfficialCompatServer::decodeStillImage(
        const std::vector<unsigned char>& encoded, int codecId,
        std::vector<DecodedFrame>& frames) const {
    const AVCodec* codec = avcodec_find_decoder(static_cast<AVCodecID>(codecId));
    AVCodecContext* codecContext = avcodec_alloc_context3(codec);
    if (codec == nullptr || codecContext == nullptr ||
            avcodec_open2(codecContext, codec, nullptr) < 0) {
        if (codecContext != nullptr) {
            avcodec_free_context(&codecContext);
        }
        LOGW_TRACE("OfficialCompatServer: codec open failed codec=%d", codecId);
        return false;
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* decodedFrame = av_frame_alloc();
    if (packet == nullptr || decodedFrame == nullptr) {
        av_packet_free(&packet);
        av_frame_free(&decodedFrame);
        avcodec_free_context(&codecContext);
        return false;
    }

    std::vector<unsigned char> padded(encoded);
    padded.resize(encoded.size() + AV_INPUT_BUFFER_PADDING_SIZE, 0);
    av_init_packet(packet);
    packet->data = padded.data();
    packet->size = static_cast<int>(encoded.size());

    bool ok = false;
    if (avcodec_send_packet(codecContext, packet) >= 0 &&
            avcodec_receive_frame(codecContext, decodedFrame) >= 0 &&
            decodedFrame->width > 0 && decodedFrame->height > 0 &&
            decodedFrame->width <= 512 && decodedFrame->height <= 512) {
        SwsContext* scaler = sws_getContext(decodedFrame->width,
                decodedFrame->height,
                static_cast<AVPixelFormat>(decodedFrame->format),
                decodedFrame->width, decodedFrame->height, AV_PIX_FMT_RGBA,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (scaler != nullptr) {
            DecodedFrame output;
            output.width = decodedFrame->width;
            output.height = decodedFrame->height;
            output.delayMs = 100;
            std::vector<unsigned char> rgba(static_cast<size_t>(output.width) *
                    static_cast<size_t>(output.height) * 4);
            uint8_t* destination[4] = {rgba.data(), nullptr, nullptr, nullptr};
            int destinationLinesize[4] = {output.width * 4, 0, 0, 0};
            if (sws_scale(scaler, decodedFrame->data,
                    decodedFrame->linesize, 0, output.height, destination,
                    destinationLinesize) > 0) {
                output.rgb.resize(static_cast<size_t>(output.width) *
                        static_cast<size_t>(output.height) * 3);
                for (size_t i = 0, j = 0; i + 3 < rgba.size();
                        i += 4, j += 3) {
                    const int alpha = rgba[i + 3];
                    output.rgb[j] = static_cast<unsigned char>(
                            rgba[i] * alpha / 255);
                    output.rgb[j + 1] = static_cast<unsigned char>(
                            rgba[i + 1] * alpha / 255);
                    output.rgb[j + 2] = static_cast<unsigned char>(
                            rgba[i + 2] * alpha / 255);
                }
                frames.push_back(output);
                ok = true;
            }
            sws_freeContext(scaler);
        }
    } else {
        LOGW_TRACE("OfficialCompatServer: still image decode failed codec=%d size=%d",
                codecId, static_cast<int>(encoded.size()));
    }

    av_frame_free(&decodedFrame);
    av_packet_free(&packet);
    avcodec_free_context(&codecContext);
    return ok;
}

void OfficialCompatServer::clearDisplay() {
    stopAnimation();
    std::vector<unsigned char> framebuffer(kDisplayWidth *
            kDisplayHeight * 3, 0);
    PageBase::sendRawLedData(framebuffer);
}

void OfficialCompatServer::showDecodeError() {
    stopAnimation();
    std::vector<unsigned char> framebuffer(kDisplayWidth *
            kDisplayHeight * 3, 0);
    for (int y = 0; y < kDisplayHeight; ++y) {
        for (int x = 0; x < kDisplayWidth; ++x) {
            if (x == y || x == kDisplayWidth - 1 - y || y == 0 ||
                    y == kDisplayHeight - 1) {
                const size_t index = (static_cast<size_t>(y) *
                        kDisplayWidth + static_cast<size_t>(x)) * 3;
                framebuffer[index] = 255;
                framebuffer[index + 1] = 0;
                framebuffer[index + 2] = 0;
            }
        }
    }
    PageBase::sendRawLedData(framebuffer);
}

void OfficialCompatServer::stopAnimation() {
    {
        std::lock_guard<std::mutex> lock(mAnimationMutex);
        mAnimationStopRequested = true;
        mAnimationRunning = false;
    }
    mAnimationCondition.notify_all();
    if (mAnimationThread.joinable()) {
        mAnimationThread.join();
    }
}

void OfficialCompatServer::displayFrames(
        const std::vector<DecodedFrame>& frames, int positionX, int positionY,
        int durationSeconds) {
    stopAnimation();
    if (frames.empty()) {
        return;
    }

    auto render = [positionX, positionY](const DecodedFrame& frame) {
        std::vector<unsigned char> framebuffer(OfficialCompatServer::kDisplayWidth *
                OfficialCompatServer::kDisplayHeight * 3, 0);
        if (OfficialCompatServer::pasteFrame(frame, positionX, positionY,
                framebuffer)) {
            PageBase::sendRawLedData(framebuffer);
        }
    };

    render(frames.front());
    if (frames.size() <= 1) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mAnimationMutex);
        mAnimationStopRequested = false;
        mAnimationRunning = true;
    }
    mAnimationThread = std::thread([this, frames, positionX, positionY,
            durationSeconds]() {
        (void) durationSeconds;
        for (;;) {
            for (const DecodedFrame& frame : frames) {
                {
                    std::lock_guard<std::mutex> lock(mAnimationMutex);
                    if (mAnimationStopRequested) {
                        mAnimationRunning = false;
                        return;
                    }
                }
                std::vector<unsigned char> framebuffer(kDisplayWidth *
                        kDisplayHeight * 3, 0);
                if (pasteFrame(frame, positionX, positionY, framebuffer)) {
                    PageBase::sendRawLedData(framebuffer);
                }
                std::unique_lock<std::mutex> lock(mAnimationMutex);
                if (mAnimationCondition.wait_for(lock,
                        std::chrono::milliseconds(frame.delayMs),
                        [this]() { return mAnimationStopRequested; })) {
                    mAnimationRunning = false;
                    return;
                }
            }
        }
    });
}

bool OfficialCompatServer::pasteFrame(const DecodedFrame& frame, int x, int y,
        std::vector<unsigned char>& framebuffer) {
    if (frame.width <= 0 || frame.height <= 0 ||
            frame.rgb.size() < static_cast<size_t>(frame.width) *
            static_cast<size_t>(frame.height) * 3) {
        return false;
    }
    for (int sourceY = 0; sourceY < frame.height; ++sourceY) {
        const int destinationY = y + sourceY;
        if (destinationY < 0 || destinationY >= kDisplayHeight) {
            continue;
        }
        for (int sourceX = 0; sourceX < frame.width; ++sourceX) {
            const int destinationX = x + sourceX;
            if (destinationX < 0 || destinationX >= kDisplayWidth) {
                continue;
            }
            const size_t sourceIndex = (static_cast<size_t>(sourceY) *
                    static_cast<size_t>(frame.width) +
                    static_cast<size_t>(sourceX)) * 3;
            const size_t destinationIndex = (static_cast<size_t>(destinationY) *
                    kDisplayWidth + static_cast<size_t>(destinationX)) * 3;
            framebuffer[destinationIndex] = frame.rgb[sourceIndex];
            framebuffer[destinationIndex + 1] = frame.rgb[sourceIndex + 1];
            framebuffer[destinationIndex + 2] = frame.rgb[sourceIndex + 2];
        }
    }
    return true;
}

std::string OfficialCompatServer::extractQueryValue(
        const std::string& target, const std::string& key) {
    const size_t queryBegin = target.find('?');
    if (queryBegin == std::string::npos) {
        return "";
    }
    const std::string query = target.substr(queryBegin + 1);
    size_t begin = 0;
    while (begin <= query.size()) {
        const size_t end = query.find('&', begin);
        const std::string pair = query.substr(begin,
                end == std::string::npos ? std::string::npos : end - begin);
        const size_t equals = pair.find('=');
        const std::string pairKey = pair.substr(0, equals);
        if (pairKey == key) {
            return urlDecode(equals == std::string::npos ? "" :
                    pair.substr(equals + 1));
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return "";
}

std::string OfficialCompatServer::urlDecode(const std::string& value) {
    std::string result;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const char high = value[i + 1];
            const char low = value[i + 2];
            if (std::isxdigit(static_cast<unsigned char>(high)) &&
                    std::isxdigit(static_cast<unsigned char>(low))) {
                const std::string hex = value.substr(i + 1, 2);
                result.push_back(static_cast<char>(std::strtol(
                        hex.c_str(), nullptr, 16)));
                i += 2;
                continue;
            }
        }
        result.push_back(value[i] == '+' ? ' ' : value[i]);
    }
    return result;
}

std::string OfficialCompatServer::jsonEscape(const std::string& value) {
    std::ostringstream escaped;
    for (char ch : value) {
        switch (ch) {
            case '\\': escaped << "\\\\"; break;
            case '"': escaped << "\\\""; break;
            case '\b': escaped << "\\b"; break;
            case '\f': escaped << "\\f"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    escaped << ' ';
                } else {
                    escaped << ch;
                }
                break;
        }
    }
    return escaped.str();
}

std::string OfficialCompatServer::stripBase64Whitespace(
        const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (char ch : value) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            result.push_back(ch);
        }
    }
    return result;
}

bool OfficialCompatServer::sendAll(int fd, const char* data, size_t size) {
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

void OfficialCompatServer::sendResponse(int fd, int status,
        const char* contentType, const std::string& body) {
    const char* statusText = "OK";
    if (status == 400) statusText = "Bad Request";
    if (status == 404) statusText = "Not Found";
    if (status == 413) statusText = "Payload Too Large";
    if (status == 415) statusText = "Unsupported Media Type";

    std::ostringstream response;
    response << "HTTP/1.1 " << status << " " << statusText << "\r\n"
             << "Content-Type: " << contentType << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n"
             << "Access-Control-Allow-Headers: Content-Type\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    const std::string serialized = response.str();
    sendAll(fd, serialized.data(), serialized.size());
}
