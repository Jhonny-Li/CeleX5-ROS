/**
 *  The ROS package for CeleX^TM CeleX5-MIPI Dynamic Vision Sensor.
 *
 *  Copyright (C) 2020  Kehan.Xue<kehan.xue@gmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "celex5_data_forwarder.h"
#include "sensor_msgs/Imu.h"
#include "dvs_msgs/EventArray.h"

celex5_ros::CeleX5DataForwarder::CeleX5DataForwarder(const ros::NodeHandle &nh,
                                                     const std::shared_ptr<CeleX5> &p_celex5_sensor)
    : nh_(nh),
      p_celex5_sensor_(p_celex5_sensor) {

  p_celex5_options_ = CeleX5Options::GetInstance();

  p_celex5_data_server_ = p_celex5_sensor->getSensorDataServer();
  p_celex5_data_server_->registerData(this, emDataType::CeleX_Frame_Data);

  CeleX5DdyConfigure::ReadROSParam(nh_, "frame_id", this->frame_id_);

  events_pub_ = nh_.advertise<celex5_msgs::EventVector>("celex_events", 10);
  celex_imu_pub_ = nh_.advertise<celex5_msgs::ImuVector>("celex_imu", 10);
  imu_pub_ = nh_.advertise<sensor_msgs::Imu>("imu", 10);
  dvs_events_pub_ = nh_.advertise<dvs_msgs::EventArray>("events", 10);
  p_polarity_img_pub_ = std::make_shared<CameraPublisher>(ros::NodeHandle(nh_, "polarity_img"), 1);
  // binary_img_pub_ = nh_.advertise<sensor_msgs::Image>("event_binary_img", 10);
  // denoised_img_pub_ = nh_.advertise<sensor_msgs::Image>("event_denoised_img", 10);;
  // count_img_pub_ = nh_.advertise<sensor_msgs::Image>("event_count_img", 10);;
  // optical_flow_img_pub_ = nh_.advertise<sensor_msgs::Image>("event_optical_flow_img", 10);;
  //
  // gray_img_pub_ = nh_.advertise<sensor_msgs::Image>("event_gray_img", 10);;
  // accumulated_img_pub_ = nh_.advertise<sensor_msgs::Image>("event_accumulated_img", 10);;
  // superimposed_img_pub_ = nh_.advertise<sensor_msgs::Image>("event_superimposed_img", 10);;
  // optical_flow_direction_img_pub_ = nh_.advertise<sensor_msgs::Image>("event_optical_flow_direction_img", 10);;
  // optical_flow_speed_img_pub_ = nh_.advertise<sensor_msgs::Image>("event_optical_flow_speed_img", 10);;

  CreatePubThreads();
}

celex5_ros::CeleX5DataForwarder::~CeleX5DataForwarder() {
  p_celex5_data_server_->unregisterData(this, emDataType::CeleX_Frame_Data);
  // if (p_raw_events_pub_thread_->joinable()) {
  //   p_raw_events_pub_thread_->join();
  // }
  // if (p_polarity_img_pub_thread_->joinable()) {
  //   p_polarity_img_pub_thread_->join();
  // }
  // if (p_imu_data_pub_thread_->joinable()) {
  //   p_imu_data_pub_thread_->join();
  // }
}

void celex5_ros::CeleX5DataForwarder::onFrameDataUpdated(CeleX5ProcessedData *p_sensor_data) {
  // clock_t time_now = clock();
  // static clock_t time_last = clock();
  // ROS_ERROR("!!!!!!!!!!!!!!!fps time!!!!!!!!!!!!!!!!");
  // ROS_ERROR("%lf ms\n\n", 1000.0*(time_now - time_last)/CLOCKS_PER_SEC);
  // time_last = time_now;

  CeleX5::CeleX5Mode current_mode = p_celex5_sensor_->getSensorFixedMode();

  if ((current_mode != CeleX5::CeleX5Mode::Full_Picture_Mode &&
      current_mode != CeleX5::CeleX5Mode::Optical_Flow_Mode &&
      !p_celex5_sensor_->isLoopModeEnabled())
      || p_celex5_sensor_->isLoopModeEnabled()) {

    // clock_t time_begin = clock();

    if (!p_celex5_sensor_->getEventDataVector(vec_events_)) {
      ROS_ERROR("Read events error!");
      return;
    }

    if (mu_raw_events_.try_lock()) {
      vec_raw_events_ = vec_events_;
      mu_raw_events_.unlock();
      cv_raw_events_.notify_all();
    }
    if (mu_polarity_img_.try_lock()) {
      vec_polarity_events_ = vec_events_;
      mu_polarity_img_.unlock();
      cv_polarity_img_.notify_all();
    }

    // ROS_ERROR("Get Events vector: %lf ms", 1000.0*(clock() - time_begin)/CLOCKS_PER_SEC);

  }

  // if (mu_imu_data_.try_lock()) {
  //   mu_imu_data_.unlock();
  cv_imu_data_.notify_all();
  // }
  // ROS_WARN("Notified!!!!");
}

void celex5_ros::CeleX5DataForwarder::CreatePubThreads() {
  CreateRawEventsPubThread();
  CreatePolarityImgPubThread();
  CreateImuDataPubThread();
}

void celex5_ros::CeleX5DataForwarder::CreateRawEventsPubThread() {
  p_raw_events_pub_thread_ = std::make_shared<std::thread>([&]() {
    while (ros::ok()) {
      std::unique_lock<std::mutex> lck(mu_raw_events_);
      cv_raw_events_.wait(lck);
      // ROS_WARN("Received Notified 1!!!!");

      if (p_celex5_options_->IsRawEventsEnabled() && !vec_raw_events_.empty()) {
        /*
         * Publish Raw Events Data
         */
        // clock_t time_begin = clock();
        CeleX5::CeleX5Mode current_mode = p_celex5_sensor_->getSensorFixedMode();

        celex5_msgs::EventVectorPtr event_vector_ptr_msg =
            boost::make_shared<celex5_msgs::EventVector>();
        if (events_pub_.getNumSubscribers() > 0) {
          event_vector_ptr_msg->header.stamp = ros::Time::now();
          event_vector_ptr_msg->header.frame_id = this->frame_id_;
          event_vector_ptr_msg->height = CELEX5_MAT_ROWS;
          event_vector_ptr_msg->width = CELEX5_MAT_COLS;
          event_vector_ptr_msg->vector_length = vec_raw_events_.size();
          event_vector_ptr_msg->events.reserve(vec_raw_events_.size());
        }

        dvs_msgs::EventArray dvs_event_array;
        dvs_event_array.header.stamp = ros::Time::now();
        dvs_event_array.header.frame_id = this->frame_id_;
        dvs_event_array.events.reserve(vec_raw_events_.size());
        dvs_event_array.height = CELEX5_MAT_ROWS;
        dvs_event_array.width = CELEX5_MAT_COLS;

        for (auto event_i : vec_raw_events_) {

          if (events_pub_.getNumSubscribers() > 0) {
            celex5_msgs::Event tmp_event;
            tmp_event.x = event_i.row;
            tmp_event.y = event_i.col;
            if (current_mode == CeleX5::Event_Off_Pixel_Timestamp_Mode) {
              tmp_event.brightness = 255;
            } else if (current_mode == CeleX5::Event_In_Pixel_Timestamp_Mode) {
              tmp_event.brightness = 255;
              tmp_event.in_pixel_timestamp = event_i.tInPixelIncreasing;
            } else if (current_mode == CeleX5::Event_Intensity_Mode) {
              tmp_event.brightness = event_i.adc;
              tmp_event.polarity = event_i.polarity;
            }
            tmp_event.off_pixel_timestamp = event_i.tOffPixelIncreasing;
            event_vector_ptr_msg->events.emplace_back(tmp_event);
          }

          dvs_msgs::Event dvs_event;
          dvs_event.ts = ros::Time::now();
          dvs_event.x = event_i.row;
          dvs_event.y = event_i.col;
          if (current_mode == CeleX5::Event_Intensity_Mode) {
            if (event_i.polarity == -1) {
              dvs_event.polarity = false;
            } else {
              dvs_event.polarity = true;
            }
          }
          dvs_event_array.events.emplace_back(dvs_event);
        }
        dvs_events_pub_.publish(dvs_event_array);

        if (events_pub_.getNumSubscribers() > 0) {
          events_pub_.publish(event_vector_ptr_msg);
        }
      }
      // ROS_WARN("Pub raw events vector: %lf ms", 1000.0*(clock() - time_begin)/CLOCKS_PER_SEC);
      // ROS_WARN("Received Notified 11!!!!");
      // }
      lck.unlock();
    }
  });
}

void celex5_ros::CeleX5DataForwarder::CreatePolarityImgPubThread() {
  p_polarity_img_pub_thread_ = std::make_shared<std::thread>([&]() {
    while (ros::ok()) {
      std::unique_lock<std::mutex> lck(mu_polarity_img_);
      cv_polarity_img_.wait(lck);
      // ROS_WARN("Received Notified 2!!!!");
      if (p_polarity_img_pub_->IsSubscribed()) {
        if (p_celex5_options_->IsPolarityImgEnabled() && !vec_polarity_events_.empty()) {

          CeleX5::CeleX5Mode current_mode = p_celex5_sensor_->getSensorFixedMode();
          if ((current_mode == CeleX5::Event_Intensity_Mode && !p_celex5_sensor_->isLoopModeEnabled())
              || (p_celex5_sensor_->isLoopModeEnabled()
                  && p_celex5_sensor_->getSensorLoopMode(2) == CeleX5::Event_Intensity_Mode)) {
            /*
             * Publish polarity image in Event Intensity Mode
             */
            // clock_t time_begin = clock();

            cv::Mat polarity_mat(800, 1280, CV_8UC3, cv::Scalar::all(255));
            int row = 0, col = 0;
            // TODO Multi-Thread
            for (auto event_i : vec_polarity_events_) {
              row = CELEX5_MAT_ROWS - 1 - event_i.row;
              // col = CELEX5_MAT_COLS - 1 - event_i.col;
              col = event_i.col;
              auto *p = polarity_mat.ptr<cv::Vec3b>(row);
              if (event_i.polarity == 1) {
                p[col][0] = 0;
                p[col][1] = 0;
                p[col][2] = 255;
              } else if (event_i.polarity == -1) {
                p[col][0] = 255;
                p[col][1] = 0;
                p[col][2] = 0;
              }
            }
            p_polarity_img_pub_->Publish(polarity_mat, "bgr8", frame_id_);
            // if (data_size > 0) {
            //   cv::imshow("Event Polarity Pic", polarity_mat);
            //   cv::waitKey(1);
            // }
            // ROS_INFO("Pub polarity img: %lf ms", 1000.0*(clock() - time_begin)/CLOCKS_PER_SEC);
          }
        }
      }
      lck.unlock();
    }
  });
}

void celex5_ros::CeleX5DataForwarder::CreateImuDataPubThread() {
  p_imu_data_pub_thread_ = std::make_shared<std::thread>([&]() {
    while (ros::ok()) {
      std::unique_lock<std::mutex> lck(mu_imu_data_);
      cv_imu_data_.wait(lck);
      // ROS_WARN("Received Notified 3!!!!");
      // if (celex_imu_pub_.getNumSubscribers() > 0 || imu_pub_.getNumSubscribers() > 0) {
      if (p_celex5_sensor_->isIMUModuleEnabled()) {
        std::vector<IMUData> vec_imus;
        p_celex5_sensor_->getIMUData(vec_imus);
        celex5_msgs::ImuVectorPtr imu_vector_ptr_msg =
            boost::make_shared<celex5_msgs::ImuVector>();
        imu_vector_ptr_msg->header.stamp = ros::Time::now();
        imu_vector_ptr_msg->header.frame_id = this->frame_id_;
        imu_vector_ptr_msg->vector_length = vec_imus.size();
        for (auto imu_i : vec_imus) {
          celex5_msgs::Imu tmp_imu_msg;
          static int seq = 0;
          tmp_imu_msg.header.seq = seq++;
          tmp_imu_msg.header.stamp = ros::Time::now();
          tmp_imu_msg.header.frame_id = this->frame_id_;
          tmp_imu_msg.gyro_x = imu_i.xGYROS;
          tmp_imu_msg.gyro_y = imu_i.yGYROS;
          tmp_imu_msg.gyro_z = imu_i.zGYROS;
          tmp_imu_msg.acc_x = imu_i.xACC;
          tmp_imu_msg.acc_y = imu_i.yACC;
          tmp_imu_msg.acc_z = imu_i.zACC;
          tmp_imu_msg.mag_x = imu_i.xMAG;
          tmp_imu_msg.mag_y = imu_i.yMAG;
          tmp_imu_msg.mag_z = imu_i.zMAG;
          imu_vector_ptr_msg->imus.emplace_back(tmp_imu_msg);

          sensor_msgs::Imu imu_msg;
          imu_msg.header.stamp = ros::Time::now();
          imu_msg.header.frame_id = this->frame_id_;
          imu_msg.angular_velocity.x = imu_i.xGYROS;
          imu_msg.angular_velocity.y = imu_i.yGYROS;
          imu_msg.angular_velocity.z = imu_i.zGYROS;
          imu_msg.linear_acceleration.x = imu_i.xACC;
          imu_msg.linear_acceleration.y = imu_i.yACC;
          imu_msg.linear_acceleration.z = imu_i.zACC;
          imu_pub_.publish(imu_msg);
        }
        celex_imu_pub_.publish(imu_vector_ptr_msg);
      }
      // }
      lck.unlock();
    }
  });
}


