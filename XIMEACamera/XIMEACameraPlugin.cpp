//
//  XIMEACameraPlugin.cpp
//  XIMEACamera
//
//  Created by Christopher Stawarz on 4/13/21.
//  Copyright © 2021 The MWorks Project. All rights reserved.
//


BEGIN_NAMESPACE_MW


class XIMEACameraPlugin : public Plugin {
    void registerComponents(boost::shared_ptr<ComponentRegistry> registry) override {
    }
};


extern "C" Plugin * getPlugin() {
    return new XIMEACameraPlugin();
}


END_NAMESPACE_MW
