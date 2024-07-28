// Copyright 2016 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <json/json.h>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "demo_interfaces/msg/hsb.hpp"

class Demo : public rclcpp::Node
{
public:
    Demo() : Node("converter")
    {
        load_parameters();

        subscription1_ = this->create_subscription<demo_interfaces::msg::Hsb>(
            dds2json_.from_topic, 10, std::bind(&Demo::dds2json_callback, this, std::placeholders::_1));
        publisher1_ = this->create_publisher<std_msgs::msg::String>(dds2json_.to_topic, 10);

        subscription2_ = this->create_subscription<std_msgs::msg::String>(
            json2dds_.from_topic, 10, std::bind(&Demo::json2dds_callback, this, std::placeholders::_1));
        publisher2_ = this->create_publisher<demo_interfaces::msg::Hsb>(json2dds_.to_topic, 10);
    }

private:
    void load_parameters() {
        rcl_interfaces::msg::ParameterDescriptor param_desc;

        declare_parameter("led_hsb.state.from", rclcpp::ParameterType::PARAMETER_STRING);
        declare_parameter("led_hsb.state.to", rclcpp::ParameterType::PARAMETER_STRING);
        declare_parameter("led_hsb.command.from", rclcpp::ParameterType::PARAMETER_STRING);
        declare_parameter("led_hsb.command.to", rclcpp::ParameterType::PARAMETER_STRING);

        load_parameter("led_hsb.state.from", dds2json_.from_topic, "state/led/hsb");
        load_parameter("led_hsb.state.to", dds2json_.to_topic, "stat/led/hsb");

        load_parameter("led_hsb.command.from", json2dds_.from_topic, "cmnd/led/hsb");
        load_parameter("led_hsb.command.to", json2dds_.to_topic, "command/led/hsb");
    }

    bool load_parameter(const std::string& key, std::string& value,
                       const std::string& default_value) {
        bool found = get_parameter_or(key, value, default_value);
        if (!found)
            RCLCPP_WARN(get_logger(), "Parameter '%s' not set, defaulting to '%s'",
                        key.c_str(), default_value.c_str());
        if (found)
            RCLCPP_DEBUG(get_logger(), "Retrieved parameter '%s' = '%s'", key.c_str(),
                         value.c_str());
        return found;
    }

    void dds2json_callback(const demo_interfaces::msg::Hsb & msg) const
    {
        Json::Value root;

        root["hue"] = msg.hue;
        root["saturation"] = msg.saturation;
        root["brightness"] = msg.brightness;

        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        builder["commonStyle"] = "None";

        auto message = std_msgs::msg::String();
        message.data = Json::writeString(builder, root);
        publisher1_->publish(message);
        RCLCPP_INFO(this->get_logger(), "Convert message from %s(DDS) to %s(JSON)", dds2json_.from_topic.c_str(), dds2json_.to_topic.c_str());
    }

    void json2dds_callback(const std_msgs::msg::String::SharedPtr msg) const
    {
        Json::CharReaderBuilder builder;
        Json::Value root;
        std::string errs;
        const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());

        if(reader->parse(msg->data.c_str(), msg->data.c_str() + msg->data.length(), &root, &errs)) {
            auto message = demo_interfaces::msg::Hsb();
            message.hue = root["hue"].asUInt();
            message.saturation = root["saturation"].asUInt();
            message.brightness = root["brightness"].asUInt();
            publisher2_->publish(message);

            RCLCPP_INFO(this->get_logger(), "Convert message from %s(JSON) to %s(DDS)", json2dds_.from_topic.c_str(), json2dds_.to_topic.c_str());
        }
        else {
            RCLCPP_WARN(this->get_logger(), "Failed to parse the JSON, errors: %s", errs.c_str());
        }
    }

    struct Interface {
        std::string from_topic;
        std::string to_topic;
    };

    Interface dds2json_;
    Interface json2dds_;

    rclcpp::Subscription<demo_interfaces::msg::Hsb>::SharedPtr subscription1_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription2_;
    
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher1_;
    rclcpp::Publisher<demo_interfaces::msg::Hsb>::SharedPtr publisher2_;
};

int main(int argc, char * argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<Demo>());
    rclcpp::shutdown();
    return 0;
}
