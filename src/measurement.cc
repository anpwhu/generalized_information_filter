#include "generalized_information_filter/measurement.h"
#include "generalized_information_filter/binary-residual.h"

namespace GIF {

MeasurementTimeline::MeasurementTimeline(const bool drop_first,
                                         const Duration& max_wait_time,
                                         const Duration& min_wait_time) {
  drop_first_ = drop_first;
  max_wait_time_ = max_wait_time;
  min_wait_time_ = min_wait_time;
  last_processed_time_ = TimePoint::min();
}

MeasurementTimeline::~MeasurementTimeline() {
}

void MeasurementTimeline::AddMeasurement(const ElementVectorBase::CPtr& meas,
                                  const TimePoint& t) {
  // Discard first measurement in binary case
  if (drop_first_ && last_processed_time_ == TimePoint::min()) {
    LOG(INFO) << "Droping first measurement" << std::endl;
    last_processed_time_ = t;
    return;
  }
  if (t <= last_processed_time_) {
    LOG(ERROR) << "Adding measurements before last processed time (will be discarded)" << std::endl;
  } else {
    std::pair<std::map<TimePoint, ElementVectorBase::CPtr>::iterator, bool> ret;
    ret = meas_map_.insert(std::pair<TimePoint, ElementVectorBase::CPtr>(t, meas));
    if (!ret.second) {
      LOG(ERROR) << "Measurement already exists!" << std::endl;
    } else {
      LOG(INFO) << "Adding measurement" << std::endl;
    }
  }
}

bool MeasurementTimeline::GetMeasurement(const TimePoint& t,
                                  ElementVectorBase::CPtr& meas) {
  auto it = meas_map_.find(t);
  if (it == meas_map_.end()) {
    return false;
  } else {
    meas = it->second;
    return true;
  }
}

void MeasurementTimeline::RemoveProcessedFirst() {
  LOG_IF(ERROR,meas_map_.size() == 0) << "No measurement to remove";
  last_processed_time_ = meas_map_.begin()->first;
  meas_map_.erase(meas_map_.begin());
}

void MeasurementTimeline::Reset() {
  meas_map_.clear();
  last_processed_time_ = TimePoint::min();
}

TimePoint MeasurementTimeline::GetLastTime() const {
  if (!meas_map_.empty()) {
    return meas_map_.rbegin()->first;
  } else {
    return last_processed_time_;
  }
}

TimePoint MeasurementTimeline::GetFirstTime() const {
  if (!meas_map_.empty()) {
    return meas_map_.begin()->first;
  } else {
    return TimePoint::max();
  }
}

bool MeasurementTimeline::GetFirst(ElementVectorBase::CPtr& meas) {
  if (!meas_map_.empty()) {
    meas = meas_map_.begin()->second;
    return true;
  } else {
    return false;
  }
}

TimePoint MeasurementTimeline::GetMaximalUpdateTime(const TimePoint& current_time) const {
  TimePoint maximalUpdateTime = current_time - max_wait_time_;
  if (!meas_map_.empty()) {
    maximalUpdateTime = std::max(maximalUpdateTime, meas_map_.rbegin()->first + min_wait_time_);
  } else {
    maximalUpdateTime = std::max(maximalUpdateTime, last_processed_time_ + min_wait_time_);
  }
  return maximalUpdateTime;
}

void MeasurementTimeline::GetAllInRange(std::set<TimePoint>& times,
                                        const TimePoint& start,
                                        const TimePoint& end) const {
  auto it = meas_map_.upper_bound(start);
  while (it != meas_map_.end() && it->first <= end) {
    times.insert(it->first);
    ++it;
  }
}

void MeasurementTimeline::GetLastInRange(std::set<TimePoint>& times,
                                         const TimePoint& start,
                                         const TimePoint& end) const {
  auto it = meas_map_.upper_bound(end);
  if (it != meas_map_.begin()) {
    --it;
    if (it->first > start) {
      times.insert(it->first);
    }
  }
}

void MeasurementTimeline::Split(const TimePoint& t0, const TimePoint& t1, const TimePoint& t2,
                                const BinaryResidualBase* res) {
  DLOG_IF(ERROR,t0 > t1 || t1 > t2) << "No chronological times";
  LOG(INFO) << "Insert measurement in " << res->name_
            << " at " << GIF::Print(t1);
  AddMeasurement(ElementVectorBase::CPtr(), t1);
  res->SplitMeasurements(t0, t1, t2, meas_map_.at(t2), meas_map_.at(t1), meas_map_.at(t2));
}

void MeasurementTimeline::Split(const std::set<TimePoint>& times, const BinaryResidualBase* res) {
  for (const auto& t : times) {
    auto it = meas_map_.lower_bound(t);
    if (it == meas_map_.end()) {
      LOG(ERROR) << "Range error while splitting while searching lower bound for "
                 << GIF::Print(t) << "! (" << res->name_ << ")" << std::endl;
      continue;
    }
    if (it->first == t) {
      // Measurement already available
      continue;
    }
    TimePoint previous = (it == meas_map_.begin()) ? last_processed_time_ : std::prev(it)->first;
    Split(previous, t, it->first, res);
  }
}

void MeasurementTimeline::Merge(const TimePoint& t0, const TimePoint& t1, const TimePoint& t2,
                                const BinaryResidualBase* res) {
  DLOG_IF(ERROR,t0 > t1 || t1 > t2) << "No chronological times";
  LOG(INFO) << "Merging measurement in " << res->name_
            << ", removed at " << GIF::Print(t1);
  res->MergeMeasurements(t0, t1, t2, meas_map_.at(t1), meas_map_.at(t2), meas_map_.at(t2));
  meas_map_.erase(t1);  // does not count as processed
}

void MeasurementTimeline::MergeUndesired(const std::set<TimePoint>& times,
                                         const BinaryResidualBase* res) {
  // Merge measurements such that only timepoints remain which are in times or
  // past its end.
  if (times.size() == 0) {
    return;
  }
  for (auto it = meas_map_.begin(); it != meas_map_.end();) {
    if (it->first > *times.rbegin()) {
      break;
    }
    if (times.count(it->first) > 0) {
      ++it;
      continue;
    }
    if (next(it) == meas_map_.end()) {
      LOG(ERROR) << "Range error while merging!" << std::endl;
      break;
    }
    TimePoint previous = (it == meas_map_.begin()) ? last_processed_time_ : std::prev(it)->first;
    ++it;  // Needs to be increment before erase
    Merge(previous, std::prev(it)->first, it->first, res);
  }
}

void MeasurementTimeline::RemoveOutdated(const TimePoint& time) {
  while (!meas_map_.empty() && meas_map_.begin()->first <= time) {
    LOG(WARNING) << "Removing outdated measurement at "
                 << GIF::Print(meas_map_.begin()->first) << "(normal at beginning)." << std::endl;
    RemoveProcessedFirst();
  }
}

std::string MeasurementTimeline::Print(const TimePoint& start, int start_offset,
                                double resolution) const {
  std::ostringstream out;
  const int width = meas_map_.empty() ? start_offset : start_offset
                    + ceil(toSec(meas_map_.rbegin()->first - start) / resolution) + 1;
  std::vector<int> counts(width, 0);
  for (auto it = meas_map_.begin(); it != meas_map_.end(); ++it) {
    const int x = start_offset + ceil(toSec(it->first - start) / resolution);
    if (x >= 0) {
      counts.at(x)++;
    }
  }
  for (const auto& c : counts) {
    if (c == 0) {
      out << "-";
    } else {
      out << c;
    }
  }
  out << std::endl;
  return out.str();
}

TimePoint MeasurementTimeline::GetLastProcessedTime() const{
  return last_processed_time_;
}

}
