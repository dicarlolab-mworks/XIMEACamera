//
//  XIMEACameraDevice.cpp
//  XIMEACamera
//
//  Created by Christopher Stawarz on 4/14/21.
//  Copyright © 2021 The MWorks Project. All rights reserved.
//

#include "XIMEACameraDevice.hpp"

#define DEVICE_MSG(msg)  "XIMEA camera: " msg ""
#define LOG_ERROR(msg, ...)  merror(M_IODEVICE_MESSAGE_DOMAIN, DEVICE_MSG(msg), __VA_ARGS__)


BEGIN_NAMESPACE_MW


BEGIN_NAMESPACE()


inline void logError(const char *msg) {
    LOG_ERROR("%s", msg);
}


inline bool logError(XI_RETURN status, const char *msg) {
    if (XI_OK == status) {
        return false;
    }
    LOG_ERROR("%s (status = %d)", msg, status);
    return true;
}


END_NAMESPACE()


const std::string XIMEACameraDevice::EXPOSURE_TIME("exposure_time");
const std::string XIMEACameraDevice::CAPTURE_INTERVAL("capture_interval");
const std::string XIMEACameraDevice::IMAGE_DATA("image_data");


void XIMEACameraDevice::describeComponent(ComponentInfo &info) {
    IODevice::describeComponent(info);
    
    info.setSignature("iodevice/ximea_camera");
    
    info.addParameter(EXPOSURE_TIME);
    info.addParameter(CAPTURE_INTERVAL);
    info.addParameter(IMAGE_DATA);
}


XIMEACameraDevice::XIMEACameraDevice(const ParameterValueMap &parameters) :
    IODevice(parameters),
    exposureTime(parameters[EXPOSURE_TIME]),
    captureInterval(parameters[CAPTURE_INTERVAL]),
    imageData(parameters[IMAGE_DATA]),
    clock(Clock::instance()),
    handle(nullptr),
    imageDataSize(0),
    imageCaptureTimeUS(-1),
    running(false)
{ }


XIMEACameraDevice::~XIMEACameraDevice() {
    if (handle) {
        logError(xiCloseDevice(handle), "Cannot close device");
    }
}


bool XIMEACameraDevice::initialize() {
    lock_guard lock(mutex);
    
    DWORD numDevices = 0;
    if (logError(xiGetNumberDevices(&numDevices), "Cannot enumerate connected devices")) {
        return false;
    }
    if (numDevices == 0) {
        logError("No devices detected");
        return false;
    }
    
    // TODO: If there's more than one device, make the user pick one by serial number.
    // For now, just open the first one.
    if (logError(xiOpenDevice(0, &handle), "Cannot open device")) {
        return false;
    }
    
    std::vector<char> deviceName(200);
    std::vector<char> deviceSerialNumber(100);
    if (logError(xiGetParamString(handle, XI_PRM_DEVICE_NAME, deviceName.data(), deviceName.size()),
                 "Cannot get device name") ||
        logError(xiGetParamString(handle, XI_PRM_DEVICE_SN, deviceSerialNumber.data(), deviceSerialNumber.size()),
                 "Cannot get device serial number"))
    {
        return false;
    }
    
    mprintf(M_IODEVICE_MESSAGE_DOMAIN,
            "Connected to XIMEA camera %s (serial number %s)",
            deviceName.data(),
            deviceSerialNumber.data());
    
    if (logError(xiSetParamInt(handle, XI_PRM_IMAGE_DATA_FORMAT, XI_MONO8),
                 "Cannot set image data format") ||
        logError(xiSetParamInt(handle, XI_PRM_TRG_SOURCE, XI_TRG_SOFTWARE),
                 "Cannot enable software triggering") ||
        logError(xiSetParamInt(handle, XI_PRM_GPO_SELECTOR, XI_GPO_PORT1),
                 "Cannot select output pin") ||
        logError(xiSetParamInt(handle, XI_PRM_GPO_MODE, XI_GPO_EXPOSURE_ACTIVE),
                 "Cannot set output pin active during exposure") ||
        logError(xiSetParamInt(handle, XI_PRM_BUFFER_POLICY, XI_BP_SAFE),
                 "Cannot configure buffer handling"))
    {
        return false;
    }
    
    if (logError(xiGetParamInt(handle, XI_PRM_IMAGE_PAYLOAD_SIZE, &imageDataSize),
                 "Cannot get image payload size"))
    {
        return false;
    }
    
    imageColorSpace = CGColorSpacePtr::created(CGColorSpaceCreateWithName(kCGColorSpaceLinearGray));
    
    return true;
}


bool XIMEACameraDevice::startDeviceIO() {
    lock_guard lock(mutex);
    
    if (!running) {
        // Get current parameter values
        const auto desiredExposureTime = int(exposureTime->getValue().getInteger());
        if (desiredExposureTime <= 0) {
            logError("Exposure time must be greater than zero");
            return false;
        }
        const auto desiredCaptureInterval = captureInterval->getValue().getInteger();
        if (desiredCaptureInterval <= 0) {
            logError("Capture interval must be greater than zero");
            return false;
        }
        
        // Set exposure time
        int actualExposureTime = 0;
        if (logError(xiSetParamInt(handle, XI_PRM_EXPOSURE, desiredExposureTime),
                     "Cannot set exposure time") ||
            logError(xiGetParamInt(handle, XI_PRM_EXPOSURE, &actualExposureTime),
                     "Cannot get exposure time"))
        {
            return false;
        }
        if (actualExposureTime != desiredExposureTime) {
            mwarning(M_IODEVICE_MESSAGE_DOMAIN,
                     DEVICE_MSG("Actual exposure time (%d us) differs from requested value (%d us)"),
                     actualExposureTime,
                     desiredExposureTime);
        }
        
        // Start acquisition
        if (logError(xiStartAcquisition(handle), "Cannot start data acquisition")) {
            return false;
        }
        
        // Start capture task
        if (!captureTask) {
            boost::weak_ptr<XIMEACameraDevice> weakThis(component_shared_from_this<XIMEACameraDevice>());
            captureTask = Scheduler::instance()->scheduleUS(FILELINE,
                                                            desiredCaptureInterval,
                                                            desiredCaptureInterval,
                                                            M_REPEAT_INDEFINITELY,
                                                            [weakThis, desiredCaptureInterval]() {
                                                                if (auto sharedThis = weakThis.lock()) {
                                                                    lock_guard lock(sharedThis->mutex);
                                                                    sharedThis->captureImage(desiredCaptureInterval);
                                                                }
                                                                return nullptr;
                                                            },
                                                            M_DEFAULT_IODEVICE_PRIORITY,
                                                            M_DEFAULT_IODEVICE_WARN_SLOP_US,
                                                            M_DEFAULT_IODEVICE_FAIL_SLOP_US,
                                                            M_MISSED_EXECUTION_DROP);
        }
        
        running = true;
    }
    
    return true;
}


bool XIMEACameraDevice::stopDeviceIO() {
    lock_guard lock(mutex);
    
    if (running) {
        if (captureTask) {
            captureTask->cancel();
            captureTask.reset();
        }
        
        // If necessary, process the last captured image
        processCapturedImage();
        
        if (logError(xiStopAcquisition(handle), "Cannot stop data acquisition")) {
            return false;
        }
        
        running = false;
    }
    
    return true;
}


void XIMEACameraDevice::captureImage(MWTime currentCaptureInterval) {
    if (!captureTask) {
        // We've already been canceled, so don't try to capture another image
        return;
    }
    
    auto status = xiSetParamInt(handle, XI_PRM_TRG_SOFTWARE, 1);
    if (status == XI_DEVICE_NOT_READY) {
        logError("Cannot trigger image capture: device not ready");
        return;
    } else if (logError(status, "Cannot trigger image capture")) {
        return;
    }
    
    // While the camera captures the next image, process the previous one
    processCapturedImage();
    
    auto imageData = CFMutableDataPtr::created(CFDataCreateMutable(kCFAllocatorDefault, imageDataSize));
    CFDataSetLength(imageData.get(), imageDataSize);
    
    XI_IMG imageInfo;
    std::memset(&imageInfo, 0, sizeof(imageInfo));
    imageInfo.size = sizeof(imageInfo);
    imageInfo.bp = CFDataGetMutableBytePtr(imageData.get());
    imageInfo.bp_size = CFDataGetLength(imageData.get());
    
    status = xiGetImage(handle, currentCaptureInterval / 1000 /*us to ms*/, &imageInfo);
    if (status == XI_TIMEOUT) {
        logError("Timeout waiting for current image");
        return;
    } else if (logError(status, "Cannot get current image")) {
        return;
    }
    
    imageCaptureTimeUS = clock->getCurrentTimeUS();
    auto imageDataProvider = cf::ObjectPtr<CGDataProviderRef>::created(CGDataProviderCreateWithCFData(imageData.get()));
    image = CGImagePtr::created(CGImageCreate(imageInfo.width,
                                              imageInfo.height,
                                              8,
                                              8,
                                              imageInfo.width + imageInfo.padding_x,
                                              imageColorSpace.get(),
                                              kCGImageAlphaNone,
                                              imageDataProvider.get(),
                                              nullptr,
                                              false,
                                              kCGRenderingIntentPerceptual));
}


void XIMEACameraDevice::processCapturedImage() {
    if (!image) {
        // No image to process
        return;
    }
    
    auto imageFileData = CFMutableDataPtr::created(CFDataCreateMutable(kCFAllocatorDefault, 0));
    auto imageDest = cf::ObjectPtr<CGImageDestinationRef>::created(CGImageDestinationCreateWithData(imageFileData.get(),
                                                                                                    kUTTypeJPEG,
                                                                                                    1,
                                                                                                    nullptr));
    
    CGImageDestinationAddImage(imageDest.get(), image.get(), nullptr);
    if (!CGImageDestinationFinalize(imageDest.get())) {
        logError("Cannot create image file from image data");
    } else {
        Datum value(reinterpret_cast<const char *>(CFDataGetBytePtr(imageFileData.get())),
                    CFDataGetLength(imageFileData.get()));
        value.setCompressible(false);  // JPEG is a compressed format, so don't try to compress again
        imageData->setValue(std::move(value), imageCaptureTimeUS);
    }
    
    image.reset();
}


END_NAMESPACE_MW
