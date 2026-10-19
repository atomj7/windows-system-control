#include "curve.hpp"

#include "util.hpp"

double duty_for_temperature(const std::vector<CurvePoint>& curve, double temp) {
  if (curve.empty()) return 40;
  if (temp <= curve.front().temp) return curve.front().duty;
  if (temp >= curve.back().temp) return curve.back().duty;
  for (size_t i = 1; i < curve.size(); ++i) {
    const auto& left = curve[i - 1];
    const auto& right = curve[i];
    if (temp > right.temp) continue;
    const double span = right.temp - left.temp;
    if (span <= 0.001) return right.duty;
    const double t = (temp - left.temp) / span;
    return left.duty + t * (right.duty - left.duty);
  }
  return curve.back().duty;
}

double clamp_duty(const Fan& fan, double duty) {
  const double floor = fan.allowStop ? 0 : fan.minDuty;
  return clamp_double(duty, floor, 100);
}

bool valid_curve(const std::vector<CurvePoint>& curve) {
  if (curve.size() < 2 || curve.size() > 8) return false;
  double previous = -1000;
  for (const auto& point : curve) {
    if (point.temp < 0 || point.temp > 120) return false;
    if (point.duty < 0 || point.duty > 100) return false;
    if (point.temp < previous + 1) return false;
    previous = point.temp;
  }
  return true;
}
