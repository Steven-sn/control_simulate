/******************************************************************************
 * Copyright 2018 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#include "modules/planning/planning_base.h"

#include <algorithm>
#include <list>
#include <vector>

#include "modules/common/time/time.h"
#include "modules/map/hdmap/hdmap_util.h"
#include "modules/planning/common/planning_gflags.h"
#include "modules/planning/proto/planning_internal.pb.h"

namespace apollo {
namespace planning {

using apollo::common::TrajectoryPoint;
using apollo::common::time::Clock;
using apollo::dreamview::Chart;
using apollo::hdmap::HDMapUtil;
using apollo::planning_internal::SLFrameDebug;
using apollo::planning_internal::SpeedPlan;
using apollo::planning_internal::STGraphDebug;

PlanningBase::~PlanningBase() {}

void PlanningBase::FillPlanningPb(const double timestamp,
                                  ADCTrajectory* const trajectory_pb) {
  trajectory_pb->mutable_header()->set_timestamp_sec(timestamp);
  if (!local_view_.prediction_obstacles->has_header()) {
    trajectory_pb->mutable_header()->set_lidar_timestamp(
        local_view_.prediction_obstacles->header().lidar_timestamp());
    trajectory_pb->mutable_header()->set_camera_timestamp(
        local_view_.prediction_obstacles->header().camera_timestamp());
    trajectory_pb->mutable_header()->set_radar_timestamp(
        local_view_.prediction_obstacles->header().radar_timestamp());
  }

  // TODO(all): integrate reverse gear
  trajectory_pb->set_gear(canbus::Chassis::GEAR_DRIVE);
  trajectory_pb->mutable_routing_header()->CopyFrom(
      local_view_.routing->header());

  if (FLAGS_use_planning_fallback &&
      trajectory_pb->trajectory_point_size() == 0) {
    SetFallbackTrajectory(trajectory_pb);
  }

  // NOTICE:
  // Since we are using the time at each cycle beginning as timestamp, the
  // relative time of each trajectory point should be modified so that we can
  // use the current timestamp in header.

  if (!FLAGS_planning_test_mode) {
    const double dt = timestamp - Clock::NowInSeconds();
    for (auto& p : *trajectory_pb->mutable_trajectory_point()) {
      p.set_relative_time(p.relative_time() + dt);
    }
  }
}

void PlanningBase::SetFallbackTrajectory(ADCTrajectory* const trajectory_pb) {
  CHECK_NOTNULL(trajectory_pb);
  // use planning trajectory from last cycle
  if (last_planning_ != nullptr) {
    const double current_time_stamp = trajectory_pb->header().timestamp_sec();
    const double pre_time_stamp = last_planning_->header().timestamp_sec();

    for (int i = 0; i < last_planning_->trajectory_point_size(); ++i) {
      const double t = last_planning_->trajectory_point(i).relative_time() +
                       pre_time_stamp - current_time_stamp;
      auto* p = trajectory_pb->add_trajectory_point();
      p->CopyFrom(last_planning_->trajectory_point(i));
      p->set_relative_time(t);
    }
  }
}

void AddStGraph(const STGraphDebug& st_graph, Chart* chart) {
  chart->set_title(st_graph.name());
  auto* options = chart->mutable_options();
  options->mutable_x()->set_min(0.0);
  options->mutable_x()->set_max(10);  // seconds
  options->mutable_y()->set_min(0.0);
  options->mutable_y()->set_max(80.0);  // meters
  for (const auto& boundary : st_graph.boundary()) {
    auto* boundary_chart = chart->add_polygon();
    for (const auto& point : boundary.point()) {
      auto* point_debug = boundary_chart->add_point();
      point_debug->set_x(point.t());
      point_debug->set_y(point.s());
    }
  }
}

void AddSlFrame(const SLFrameDebug& sl_frame, Chart* chart) {
  chart->set_title(sl_frame.name());
  auto* options = chart->mutable_options();
  options->mutable_x()->set_min(0.0);
  options->mutable_x()->set_max(80.0);  // s, meters
  options->mutable_y()->set_min(-8.0);
  options->mutable_y()->set_max(8.0);  // l, meters
  auto* sl_line = chart->add_line();
  sl_line->set_label("SL Path");
  for (const auto& sl_point : sl_frame.sl_path()) {
    auto* point_debug = sl_line->add_point();
    point_debug->set_x(sl_point.s());
    point_debug->set_x(sl_point.l());
  }
}

void AddSpeedPlan(
    const ::google::protobuf::RepeatedPtrField<SpeedPlan>& speed_plans,
    Chart* chart) {
  chart->set_title("Speed Plan");
  auto* options = chart->mutable_options();
  options->mutable_x()->set_min(0.0);
  options->mutable_x()->set_max(80.0);  // s, meters
  options->mutable_y()->set_min(0.0);
  options->mutable_y()->set_max(50.0);  // v, m/s
  for (const auto& speed_plan : speed_plans) {
    auto* line = chart->add_line();
    line->set_label(speed_plan.name());
    for (const auto& point : speed_plan.speed_point()) {
      auto* point_debug = line->add_point();
      point_debug->set_x(point.s());
      point_debug->set_y(point.v());
    }
  }
}

void PlanningBase::ExportChart(const planning_internal::Debug& debug_info,
                               planning_internal::Debug* debug_chart) {
  for (const auto& st_graph : debug_info.planning_data().st_graph()) {
    AddStGraph(st_graph, debug_chart->mutable_planning_data()->add_chart());
  }
  for (const auto& sl_frame : debug_info.planning_data().sl_frame()) {
    AddSlFrame(sl_frame, debug_chart->mutable_planning_data()->add_chart());
  }

  AddSpeedPlan(debug_info.planning_data().speed_plan(),
               debug_chart->mutable_planning_data()->add_chart());
  // TODO(all) add more debug for visualization if needed.
}

}  // namespace planning
}  // namespace apollo
