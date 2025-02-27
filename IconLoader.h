//
// Created by Melanie on 04/01/2025.
//

#ifndef ESP32_S3_TOUCH_LCD_2_1_ICONLOADER_H
#define ESP32_S3_TOUCH_LCD_2_1_ICONLOADER_H

#include <string>
#include <list>
#include <map>
#include <set>
#include <vector>
#include "esp_http_client.h"

using namespace std;

struct IconInfo
{
    string name;
    int status_code;
    vector<uint8_t> data{};
    vector<uint8_t> pngdata{};
    bool hasAlpha;
    int width;
    int height;
};

class IconReceiver
{
public:
    virtual void receiveIcon(IconInfo *info) = 0;
};

class IconLoader
{
public:
    static void init(string baseUrl);
    static void addIconFromData(string name, const uint8_t* data, int idth, int height);
    static void fetchIcon(const string& name, int size = 64);
    static void registerReceiver(IconReceiver *inst);
    static void unregisterReceiver(IconReceiver *inst);
    static vector<uint8_t> getIcon(const string& name, int width, int height);

private:
    static void NetworkQueueTask(void *user);
    static int ImageLoadEvent(esp_http_client_event *ev);
    static void Dispatch(IconInfo *info);
    static void decodeIcon(const shared_ptr<IconInfo>& info);
    static IconInfo *findIcon(const string& name, int width, int height);

    static set<IconReceiver *> m_receivers;
    static mutex m_receiverLock;
    static list<shared_ptr<IconInfo>> m_icons;
    static mutex m_netRequestQueueLock;
    static mutex m_iconLock;
    static list<shared_ptr<IconInfo>> m_netRequestQueue;
    static string m_baseUrl;
};


#endif //ESP32_S3_TOUCH_LCD_2_1_ICONLOADER_H
