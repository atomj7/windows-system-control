#pragma once

#include "model.hpp"

// Linear duty between temperature points. Points should be sorted by temp.
double duty_for_temperature(const std::vector<CurvePoint>& curve, double temp);

// Floors the duty so a fan or pump is not stopped unless the user allowed it.
double clamp_duty(const Fan& fan, double duty);

bool valid_curve(const std::vector<CurvePoint>& curve);
