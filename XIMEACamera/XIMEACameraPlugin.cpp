//
//  XIMEACameraPlugin.cpp
//  XIMEACamera
//
//  Created by Christopher Stawarz on 4/13/21.
//  Copyright © 2021 The MWorks Project. All rights reserved.
//

#include "XIMEACameraDevice.hpp"


BEGIN_NAMESPACE_MW


class XIMEACameraPlugin : public Plugin {
    void registerComponents(boost::shared_ptr<ComponentRegistry> registry) override {
        registry->registerFactory<StandardComponentFactory, XIMEACameraDevice>();
    }
};


extern "C" Plugin * getPlugin() {
    return new XIMEACameraPlugin();
}


END_NAMESPACE_MW
