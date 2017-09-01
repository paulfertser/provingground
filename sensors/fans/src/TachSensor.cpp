#include <limits.h>
#include <unistd.h>
#include <TachSensor.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <dbus/connection.hpp>
#include <dbus/endpoint.hpp>
#include <dbus/message.hpp>
#include <iostream>
#include <string>

#define PWM_POLL_MS 500

TachSensor::TachSensor(std::string& path, dbus::DbusObjectServer& object_server,
                       boost::asio::io_service& io, std::string& fan_name,
                       nlohmann::json& json_config)
    : path(path),
      object_server(object_server),
      io(io),
      name(boost::replace_all_copy(fan_name, " ", "_")),
      json_config(json_config),
      dbus_object(object_server.add_object(
          "/xyz/openbmc_project/sensors/tach/" + name)),
      dbus_interface(dbus_object->add_interface("xyz.openbmc_project.Sensor")),
      input_dev(io, open(path.c_str(), O_RDONLY)),
      wait_timer(io),
      value(FLT_MAX),
      err_count(0) {
  setup_read();
}

void TachSensor::setup_read(void) {
  boost::asio::async_read_until(
      input_dev, read_buf, '\n',
      [&](const boost::system::error_code& ec,
          std::size_t /*bytes_transfered*/) { handle_response(ec); });
}

void TachSensor::handle_response(const boost::system::error_code& err) {
  if (!err) {
    err_count = 0;
    std::istream response_stream(&read_buf);
    std::string response;
    std::getline(response_stream, response);
    float nvalue = std::stof(response);
    response_stream.clear();
    if (nvalue != value) update_value(nvalue);
  } else {
    // todo(james) this happens mostly on poweroff, where we shouldn't be
    // reading anyway

    std::cerr << "Failure to read sensor " << name << " at " << path << "\n";

    if (err_count >= 10) {
      // only send value update once
      if (err_count == 10) {
        update_value(0);
        err_count++;
      }
    } else
      err_count++;
  }
  input_dev.close();
  input_dev.assign(open(path.c_str(), O_RDONLY));
  wait_timer.expires_from_now(boost::posix_time::milliseconds(PWM_POLL_MS));
  wait_timer.async_wait(
      [&](const boost::system::error_code&) { setup_read(); });
}

void TachSensor::check_thresholds(float new_value) {
  int index = boost::ends_with(name, "b") ? 1 : 0;
  auto thresholds = json_config["thresholds"].at(index);
  for (auto& threshold : thresholds) {
    if (threshold["direction"] == "less than") {
      if (value >= threshold["value"] && new_value < threshold["value"]) {
        std::cout << "threshold crossed!\n";  // todo(james) assert on dbus
      } else if (value < threshold["value"] &&
                 new_value >= threshold["value"]) {
        std::cout << "threshold uncrossed!\n";
      }
    } else if (threshold["direction"] == "greater than") {
      if (value <= threshold["value"] && new_value > threshold["value"]) {
        std::cout << "threshold crossed!\n";  // todo(james) assert on dbus
      } else if (value > threshold["value"] &&
                 new_value <= threshold["value"]) {
        std::cout << "threshold uncrossed!\n";
      }

    } else {
      std::cerr << "Invalid threshold " << threshold["direction"] << ".\n";
    }
  }
}

void TachSensor::update_value(float new_value) {
  check_thresholds(new_value);
  dbus_interface->set_property("Value", new_value);
}
