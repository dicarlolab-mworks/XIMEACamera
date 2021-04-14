//
//  XIMEACameraDevice.hpp
//  XIMEACamera
//
//  Created by Christopher Stawarz on 4/14/21.
//  Copyright © 2021 The MWorks Project. All rights reserved.
//

#ifndef XIMEACameraDevice_hpp
#define XIMEACameraDevice_hpp


BEGIN_NAMESPACE_MW


class XIMEACameraDevice : public IODevice, boost::noncopyable {
    
public:
    static const std::string EXPOSURE_TIME;
    static const std::string CAPTURE_INTERVAL;
    static const std::string IMAGE_DATA;
    
    static void describeComponent(ComponentInfo &info);
    
    explicit XIMEACameraDevice(const ParameterValueMap &parameters);
    ~XIMEACameraDevice();
    
    bool initialize() override;
    bool startDeviceIO() override;
    bool stopDeviceIO() override;
    
private:
    using CFMutableDataPtr = cf::ObjectPtr<CFMutableDataRef>;
    using CGColorSpacePtr = cf::ObjectPtr<CGColorSpaceRef>;
    using CGImagePtr = cf::ObjectPtr<CGImageRef>;
    
    void captureImage(MWTime currentCaptureInterval);
    void processCapturedImage();
    
    const VariablePtr exposureTime;
    const VariablePtr captureInterval;
    const VariablePtr imageData;
    
    const boost::shared_ptr<Clock> clock;
    
    HANDLE handle;
    
    int imageDataSize;
    CGColorSpacePtr imageColorSpace;
    MWTime imageCaptureTimeUS;
    CGImagePtr image;
    
    boost::shared_ptr<ScheduleTask> captureTask;
    
    bool running;
    
    using lock_guard = std::lock_guard<std::mutex>;
    lock_guard::mutex_type mutex;
    
};


END_NAMESPACE_MW


#endif /* XIMEACameraDevice_hpp */
