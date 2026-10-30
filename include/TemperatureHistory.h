#pragma once
#include <cmath>
#include <cstdint>

// One UTC minute per slot. The caller owns synchronization; no storage writes.
class TemperatureHistory {
 public:
  static constexpr unsigned kCapacity = 1440;
  struct Bucket {
    int64_t minute = -1;
    float boiler_min = NAN, boiler_max = NAN;
    float solar_min = NAN, solar_max = NAN;
  };
  void expire(int64_t now_epoch) {
    if (now_epoch < 1700000000) { clear(); return; }
    if (last_now_ && now_epoch + 60 < last_now_) clear();
    last_now_ = now_epoch;
    for (auto& bucket : buckets_)
      if (bucket.minute >= 0 &&
          (bucket.minute * 60 < now_epoch - 86400 || bucket.minute * 60 > now_epoch + 30))
        bucket = Bucket{};
  }
  bool add(int64_t observed_epoch, int64_t now_epoch, float boiler, float solar,
           bool reported) {
    expire(now_epoch);
    if (!reported || !std::isfinite(boiler) || !std::isfinite(solar) ||
        boiler < -50 || boiler > 150 || solar < -50 || solar > 150 ||
        observed_epoch < now_epoch - 86400 || observed_epoch > now_epoch + 30)
      return false;
    const int64_t minute = observed_epoch / 60;
    Bucket& bucket = buckets_[static_cast<unsigned>(minute % kCapacity)];
    if (bucket.minute != minute) {
      bucket = Bucket{};
      bucket.minute = minute;
      bucket.boiler_min = bucket.boiler_max = boiler;
      bucket.solar_min = bucket.solar_max = solar;
    } else {
      if (boiler < bucket.boiler_min) bucket.boiler_min = boiler;
      if (boiler > bucket.boiler_max) bucket.boiler_max = boiler;
      if (solar < bucket.solar_min) bucket.solar_min = solar;
      if (solar > bucket.solar_max) bucket.solar_max = solar;
    }
    return true;
  }
  Bucket at(int64_t minute) const {
    if (minute < 0) return {};
    const Bucket& bucket = buckets_[static_cast<unsigned>(minute % kCapacity)];
    return bucket.minute == minute ? bucket : Bucket{};
  }
  void clear() { for (auto& bucket : buckets_) bucket = Bucket{}; last_now_ = 0; }
 private:
  Bucket buckets_[kCapacity]{};
  int64_t last_now_ = 0;
};
