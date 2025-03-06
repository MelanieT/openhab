//
// Created by Melanie on 26/01/2025.
//

#ifndef LIGHTSWITCH_OPENHABOBJECT_H
#define LIGHTSWITCH_OPENHABOBJECT_H

#include <string>
#include <vector>
#include "nlohmann/json.hpp"

using namespace std;
using namespace nlohmann;

enum Type
{
    Root,
    Switch,
    Slider,
    Text,
    Colorpicker,
    Group,
    Frame,
    Rollershutter,
    Unknown
};

enum ItemType
{
    SwitchItem,
    DimmerItem,
    ColorItem,
    GroupItem,
    UnknownItem,
    NumberItem,
    StringItem,
};

struct StateDescription
{
    float step;
    string pattern;
    bool readOnly;
    vector<string> options;
};

struct OpenHabItem
{
    string link;
    string state;
    StateDescription stateDescription;
    ItemType type;
    string name;
    string label;
    string category;
    string unitSymbol;
    // Omitted tags and groupNames to save memory
};

struct OpenHabParent
{
    string id;
    string title;
    string link;
    // Omitted leaf, timeout
};

struct LinkedPage
{
    string id;
    string title;
    string icon;
    string link;
    bool leaf;
    // Omitted: timeout
};

struct OpenHabMapping
{
    string command;
    string label;
};

struct OpenHabWidget
{
    string widgetId;
    Type type;
    bool visibility;
    string label;
    string labelSource;
    string icon;
    bool staticIcon;
    string pattern;
    string unit;
    bool leaf;
    vector<OpenHabMapping> mappings;
    OpenHabItem item;
    vector<shared_ptr<OpenHabItem>> children;
    OpenHabParent parent;
    LinkedPage linkedPage;
};

class OpenHabObject
{
public:
    OpenHabObject(OpenHabObject *parent, const json& data);
    virtual ~OpenHabObject() = default;

    [[nodiscard]] inline Type type() const { return m_widgetData.type; };
    [[nodiscard]] inline ItemType itemType() const { return m_widgetData.item.type; };

    inline vector<shared_ptr<OpenHabObject>> children() { return m_children; };
    inline void addChild(const shared_ptr<OpenHabObject>& child) { m_children.emplace_back(child); };
    virtual void handleEvent(string target, json& evt);
    void deleteChildren();

protected:
    inline OpenHabObject *parent() { return m_parent; }
    OpenHabWidget m_widgetData;

private:
    OpenHabObject *m_parent;
    vector<shared_ptr<OpenHabObject>> m_children;
};


#endif //LIGHTSWITCH_OPENHABOBJECT_H
