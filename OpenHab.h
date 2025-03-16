//
// Created by Melanie on 25/01/2025.
//

#ifndef LIGHTSWITCH_OPENHAB_H
#define LIGHTSWITCH_OPENHAB_H

#include <string>
#include <utility>
#include <esp_http_client.h>
#include <list>

#include "nlohmann/json.hpp"
#include "OpenHabObject.h"
#include "OpenHabFactory.h"
#include "HttpRequests.h"

using namespace std;
using namespace nlohmann;

struct Sitemap
{
    string name;
    string label;
    string homepage;
};

class OpenHab
{
public:
    OpenHab();
    explicit OpenHab(string baseUrl);
    OpenHab(string baseUrl, string apiToken);
    ~OpenHab() = default;

    void connectEventChannel();
    void disconnectEventChannel();

    inline string baseUrl() { return m_baseUrl; };
    inline void setBaseUrl(string url) { m_baseUrl = move(url); };
    inline void setApiKey(string apiKey) { m_apiToken = move(apiKey);};
    inline void setEventTaskStackSize(int stackSize) { m_eventTaskStackSize = stackSize; };

    int loadSitemapsList(const function<void(vector<Sitemap>)>&);
    int loadSitemap(const string& url, OpenHabFactory *factory, const function<void(shared_ptr<OpenHabObject>)>& func, void *userData);

    static Type mapType(const json& data);
    static ItemType mapItemType(const json& data);

    static void registerEventHandler(OpenHabObject *obj);
    static void unregisterEventHandler(OpenHabObject *obj);

protected:
    string m_baseUrl;
    string m_apiToken;
    shared_ptr<OpenHabObject> m_root = nullptr;

private:
    static void openHabEventTask(void *user);
    static int openHabEvent(esp_http_client_event *user);
    void sendStatusUpdate(json& msg);
    virtual void handleSitemap(vector<uint8_t>& data, OpenHabFactory *factory, const function<void(shared_ptr<OpenHabObject>)>& func, void *userData);
    virtual void handleSitemaps(vector<uint8_t>& data, const function<void(vector<Sitemap>)>& func);
    void addChildrenRecursive(shared_ptr<OpenHabObject>& current, const json& data, OpenHabFactory *factory);

    int m_eventTaskStackSize = 4096;
    volatile TaskHandle_t m_eventTask = nullptr;
    char *m_eventUrl;
    static list<OpenHabObject *> m_eventHandlers;
    volatile bool m_abort = false;
    esp_http_client *m_handle;
};


#endif //LIGHTSWITCH_OPENHAB_H
