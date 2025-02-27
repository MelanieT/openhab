//
// Created by Melanie on 26/01/2025.
//

#ifndef LIGHTSWITCH_OPENHABFACTORY_H
#define LIGHTSWITCH_OPENHABFACTORY_H


#include "OpenHabObject.h"

class OpenHabFactory
{
public:
    OpenHabFactory() = default;
    virtual ~OpenHabFactory() = default;

    virtual OpenHabObject *createObject(OpenHabObject *parent, const json& data, Type type, void *userData);
};


#endif //LIGHTSWITCH_OPENHABFACTORY_H
