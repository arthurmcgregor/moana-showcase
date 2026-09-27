#include "ui/screen_context.h"

#include "ui/display.h"

void ScreenContext::init(Display* display, PostEventFn post, TrackCountFn trackCount) {
  display_ = display;
  post_ = std::move(post);
  trackCount_ = std::move(trackCount);
}

void ScreenContext::show(ScreenState s) {
  display_->showScreen(s);
}

void ScreenContext::back() {
  display_->showScreen(ScreenState::MAIN_MENU);
}

ScreenState ScreenContext::current() const {
  return display_->getScreenState();
}

GPSState ScreenContext::gpsState() const {
  return display_->getGPSState();
}

void ScreenContext::toggleRecording()    { post_(ButtonId::TOGGLE_RECORDING); }
void ScreenContext::toggleWifi()         { post_(ButtonId::TOGGLE_WIFI); }
void ScreenContext::requestSync()        { post_(ButtonId::START_SYNC); }
void ScreenContext::format()             { post_(ButtonId::FORMAT); }
void ScreenContext::startCalibration()   { post_(ButtonId::START_IMU_CALIBRATION); }
void ScreenContext::cancelCalibration()  { post_(ButtonId::CANCEL_IMU_CALIBRATION); }
void ScreenContext::applyCalibration()   { post_(ButtonId::APPLY_IMU_CALIBRATION); }
void ScreenContext::discardCalibration() { post_(ButtonId::DISCARD_IMU_CALIBRATION); }
