//
// Created by Melanie on 26/01/2025.
//

#ifndef LIGHTSWITCH_HTTPREQUESTS_H
#define LIGHTSWITCH_HTTPREQUESTS_H

#include <string>
#include <utility>
#include <vector>
#include <memory>

using namespace std;

class HttpRequests
{
public:
    static esp_err_t postUrl(const std::string& url, std::vector<uint8_t>& data, const function<void(vector<uint8_t>&)>& func);
    static esp_err_t getUrl(const std::string& url, const function<void(vector<uint8_t>&)>& func);
    static esp_err_t DownloadHandler(esp_http_client_event *ev);

    static inline void setApiToken(std::string apiToken) {
        m_apiToken = std::move(apiToken);
    };

private:
    struct DownloadInfo
    {
        std::vector<uint8_t> data{};
    };
    static mutex m_httpLock;
    static std::string m_apiToken;
};


#endif //LIGHTSWITCH_HTTPREQUESTS_H
