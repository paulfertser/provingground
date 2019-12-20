#include "logger.hpp"
#include "system.hpp"

#include <sys/mount.h>

#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <boost/process.hpp>
#include <filesystem>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

class App
{
  public:
    App(boost::asio::io_context& ioc, sd_bus* custom_bus = nullptr) :
        ioc(ioc), devMonitor(ioc)
    {
        if (!custom_bus)
        {
            bus = std::make_shared<sdbusplus::asio::connection>(ioc);
        }
        else
        {
            bus =
                std::make_shared<sdbusplus::asio::connection>(ioc, custom_bus);
        }
        objServer = std::make_shared<sdbusplus::asio::object_server>(bus);
        bus->request_name("xyz.openbmc_project.VirtualMedia");
        objManager = std::make_shared<sdbusplus::server::manager::manager>(
            *bus, "/xyz/openbmc_project/VirtualMedia");

        devMonitor.run([](const NBDDevice& device, StateChange change) {
            // placeholder for some future actions
        });
    }

  private:
    boost::asio::io_context& ioc;
    std::shared_ptr<sdbusplus::asio::connection> bus;
    std::shared_ptr<sdbusplus::asio::object_server> objServer;
    std::shared_ptr<sdbusplus::server::manager::manager> objManager;
    DeviceMonitor devMonitor;
};

int main()
{
    boost::asio::io_context ioc;
    boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait(
        [&ioc](const boost::system::error_code&, const int&) { ioc.stop(); });

    sd_bus* b = nullptr;
#if defined(CUSTOM_DBUS_PATH)
#pragma message("You are using custom DBUS path set to " CUSTOM_DBUS_PATH)
    sd_bus_new(&b);
    sd_bus_set_bus_client(b, true);
    sd_bus_set_address(b, CUSTOM_DBUS_PATH);
    sd_bus_start(b);
#endif
    sd_bus_default_system(&b);
    App app(ioc, b);

    ioc.run();

    return 0;
}
