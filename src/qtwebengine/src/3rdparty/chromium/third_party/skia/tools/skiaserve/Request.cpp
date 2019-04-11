/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "Request.h"

#include "SkPictureRecorder.h"
#include "sk_tool_utils.h"

using namespace sk_gpu_test;

static int kDefaultWidth = 1920;
static int kDefaultHeight = 1080;
static int kMaxWidth = 8192;
static int kMaxHeight = 8192;


Request::Request(SkString rootUrl)
    : fUploadContext(nullptr)
    , fUrlDataManager(rootUrl)
    , fGPUEnabled(false)
    , fOverdraw(false)
    , fColorMode(0) {
    // create surface
    GrContextOptions grContextOpts;
    fContextFactory = new GrContextFactory(grContextOpts);
}

Request::~Request() {
    if (fContextFactory) {
        delete fContextFactory;
    }
}

sk_sp<SkData> Request::writeCanvasToPng(SkCanvas* canvas) {
    // capture pixels
    SkBitmap bmp;
    bmp.allocPixels(canvas->imageInfo());
    SkAssertResult(canvas->readPixels(bmp, 0, 0));

    // write to an opaque png (black background)
    SkDynamicMemoryWStream buffer;
    SkDrawCommand::WritePNG(bmp, buffer);
    return buffer.detachAsData();
}

SkCanvas* Request::getCanvas() {
    GrContextFactory* factory = fContextFactory;
    GLTestContext* gl = factory->getContextInfo(GrContextFactory::kGL_ContextType,
            GrContextFactory::ContextOverrides::kNone).glContext();
    if (!gl) {
        gl = factory->getContextInfo(GrContextFactory::kGLES_ContextType,
                                     GrContextFactory::ContextOverrides::kNone).glContext();
    }
    if (gl) {
        gl->makeCurrent();
    }
    SkASSERT(fDebugCanvas);

    // create the appropriate surface if necessary
    if (!fSurface) {
        this->enableGPU(fGPUEnabled);
    }
    SkCanvas* target = fSurface->getCanvas();
    return target;
}

sk_sp<SkData> Request::drawToPng(int n, int m) {
    //fDebugCanvas->setOverdrawViz(true);
    fDebugCanvas->drawTo(this->getCanvas(), n, m);
    //fDebugCanvas->setOverdrawViz(false);
    return writeCanvasToPng(this->getCanvas());
}

sk_sp<SkData> Request::writeOutSkp() {
    // Playback into picture recorder
    SkIRect bounds = this->getBounds();
    SkPictureRecorder recorder;
    SkCanvas* canvas = recorder.beginRecording(SkIntToScalar(bounds.width()),
                                               SkIntToScalar(bounds.height()));

    fDebugCanvas->draw(canvas);

    return recorder.finishRecordingAsPicture()->serialize();
}

GrContext* Request::getContext() {
    GrContext* result = fContextFactory->get(GrContextFactory::kGL_ContextType,
                                             GrContextFactory::ContextOverrides::kNone);
    if (!result) {
        result = fContextFactory->get(GrContextFactory::kGLES_ContextType,
                                      GrContextFactory::ContextOverrides::kNone);
    }
    return result;
}

SkIRect Request::getBounds() {
    SkIRect bounds;
    if (fPicture) {
        bounds = fPicture->cullRect().roundOut();
        if (fGPUEnabled) {
            int maxRTSize = this->getContext()->maxRenderTargetSize();
            bounds = SkIRect::MakeWH(SkTMin(bounds.width(), maxRTSize),
                                     SkTMin(bounds.height(), maxRTSize));
        }
    } else {
        bounds = SkIRect::MakeWH(kDefaultWidth, kDefaultHeight);
    }

    // We clip to kMaxWidth / kMaxHeight for performance reasons.
    // TODO make this configurable
    bounds = SkIRect::MakeWH(SkTMin(bounds.width(), kMaxWidth),
                             SkTMin(bounds.height(), kMaxHeight));
    return bounds;
}

namespace {

struct ColorAndProfile {
    SkColorType fColorType;
    bool fSRGB;
};

ColorAndProfile ColorModes[] = {
    { kN32_SkColorType,      false },
    { kN32_SkColorType,       true },
    { kRGBA_F16_SkColorType,  true },
};

}

SkSurface* Request::createCPUSurface() {
    SkIRect bounds = this->getBounds();
    ColorAndProfile cap = ColorModes[fColorMode];
    auto colorSpace = kRGBA_F16_SkColorType == cap.fColorType
                    ? SkColorSpace::MakeSRGBLinear()
                    : SkColorSpace::MakeSRGB();
    SkImageInfo info = SkImageInfo::Make(bounds.width(), bounds.height(), cap.fColorType,
                                         kPremul_SkAlphaType, cap.fSRGB ? colorSpace : nullptr);
    return SkSurface::MakeRaster(info).release();
}

SkSurface* Request::createGPUSurface() {
    GrContext* context = this->getContext();
    SkIRect bounds = this->getBounds();
    ColorAndProfile cap = ColorModes[fColorMode];
    auto colorSpace = kRGBA_F16_SkColorType == cap.fColorType
                    ? SkColorSpace::MakeSRGBLinear()
                    : SkColorSpace::MakeSRGB();
    SkImageInfo info = SkImageInfo::Make(bounds.width(), bounds.height(), cap.fColorType,
                                         kPremul_SkAlphaType, cap.fSRGB ? colorSpace: nullptr);
    SkSurface* surface = SkSurface::MakeRenderTarget(context, SkBudgeted::kNo, info).release();
    return surface;
}

bool Request::setOverdraw(bool enable) {
    fOverdraw = enable;
    return true;
}

bool Request::setColorMode(int mode) {
    fColorMode = mode;
    return enableGPU(fGPUEnabled);
}

bool Request::enableGPU(bool enable) {
    if (enable) {
        SkSurface* surface = this->createGPUSurface();
        if (surface) {
            fSurface.reset(surface);
            fGPUEnabled = true;

            // When we switch to GPU, there seems to be some mystery draws in the canvas.  So we
            // draw once to flush the pipe
            // TODO understand what is actually happening here
            if (fDebugCanvas) {
                fDebugCanvas->drawTo(this->getCanvas(), this->getLastOp());
                this->getCanvas()->flush();
            }

            return true;
        }
        return false;
    }
    fSurface.reset(this->createCPUSurface());
    fGPUEnabled = false;
    return true;
}

bool Request::initPictureFromStream(SkStream* stream) {
    // parse picture from stream
    fPicture = SkPicture::MakeFromStream(stream);
    if (!fPicture) {
        fprintf(stderr, "Could not create picture from stream.\n");
        return false;
    }

    // reinitialize canvas with the new picture dimensions
    this->enableGPU(fGPUEnabled);

    // pour picture into debug canvas
    SkIRect bounds = this->getBounds();
    fDebugCanvas.reset(new SkDebugCanvas(bounds.width(), bounds.height()));
    fDebugCanvas->drawPicture(fPicture);

    // for some reason we need to 'flush' the debug canvas by drawing all of the ops
    fDebugCanvas->drawTo(this->getCanvas(), this->getLastOp());
    this->getCanvas()->flush();
    return true;
}

sk_sp<SkData> Request::getJsonOps(int n) {
    SkCanvas* canvas = this->getCanvas();
    Json::Value root = fDebugCanvas->toJSON(fUrlDataManager, n, canvas);
    root["mode"] = Json::Value(fGPUEnabled ? "gpu" : "cpu");
    root["drawGpuOpBounds"] = Json::Value(fDebugCanvas->getDrawGpuOpBounds());
    root["colorMode"] = Json::Value(fColorMode);
    SkDynamicMemoryWStream stream;
    stream.writeText(Json::FastWriter().write(root).c_str());

    return stream.detachAsData();
}

sk_sp<SkData> Request::getJsonOpList(int n) {
    SkCanvas* canvas = this->getCanvas();
    SkASSERT(fGPUEnabled);

    Json::Value result = fDebugCanvas->toJSONOpList(n, canvas);

    SkDynamicMemoryWStream stream;
    stream.writeText(Json::FastWriter().write(result).c_str());

    return stream.detachAsData();
}

sk_sp<SkData> Request::getJsonInfo(int n) {
    // drawTo
    sk_sp<SkSurface> surface(this->createCPUSurface());
    SkCanvas* canvas = surface->getCanvas();

    // TODO this is really slow and we should cache the matrix and clip
    fDebugCanvas->drawTo(canvas, n);

    // make some json
    SkMatrix vm = fDebugCanvas->getCurrentMatrix();
    SkIRect clip = fDebugCanvas->getCurrentClip();
    Json::Value info(Json::objectValue);
    info["ViewMatrix"] = SkDrawCommand::MakeJsonMatrix(vm);
    info["ClipRect"] = SkDrawCommand::MakeJsonIRect(clip);

    std::string json = Json::FastWriter().write(info);

    // We don't want the null terminator so strlen is correct
    return SkData::MakeWithCopy(json.c_str(), strlen(json.c_str()));
}

SkColor Request::getPixel(int x, int y) {
    SkBitmap bmp;
    bmp.allocPixels(this->getCanvas()->imageInfo().makeWH(1, 1));
    SkAssertResult(this->getCanvas()->readPixels(bmp, x, y));
    return bmp.getColor(0, 0);
}