#pragma once
#include "esphome/core/component.h"
namespace esphome::sensor { class Sensor { public: float state=NAN; void publish_state(float v) {state=v;} }; }
