
#define ELPP_DEFAULT_LOG_FILE "var/log/swgchat.log"
#include "easylogging++.h"

#include "StationChatApp.hpp"

#include <boost/program_options.hpp>

#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

#ifdef __GNUC__
#include <execinfo.h>
#endif

INITIALIZE_EASYLOGGINGPP

StationChatConfig BuildConfiguration(int argc, const char* argv[]);

#ifdef __GNUC__
void SignalHandler(int sig);
#endif

int main(int argc, const char* argv[]) {
#ifdef __GNUC__
    signal(SIGSEGV, SignalHandler);
#endif

    auto config = BuildConfiguration(argc, argv);

    el::Loggers::setDefaultConfigurations(config.loggerConfig, true);
    START_EASYLOGGINGPP(argc, argv);

    StationChatApp app{config};

    while (app.IsRunning()) {
        app.Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}

StationChatConfig BuildConfiguration(int argc, const char* argv[]) {
    namespace po = boost::program_options;
    StationChatConfig config;
    std::string configFile;

    // Declare a group of options that will be 
    // allowed only on command line
    po::options_description generic("Generic options");
    generic.add_options()
        ("help,h", "produces help message")
        ("config,c", po::value<std::string>(&configFile)->default_value("etc/stationapi/swgchat.cfg"),
            "sets path to the configuration file")
        ("logger_config", po::value<std::string>(&config.loggerConfig)->default_value("etc/stationapi/logger.cfg"),
            "sets path to the logger configuration file")
        ;

    po::options_description options("Configuration");
    options.add_options()
        ("gateway_address", po::value<std::string>(&config.gatewayAddress)->default_value("127.0.0.1"),
            "address for gateway connections")
        ("gateway_port", po::value<uint16_t>(&config.gatewayPort)->default_value(5001),
            "port for gateway connections")
        ("registrar_address", po::value<std::string>(&config.registrarAddress)->default_value("127.0.0.1"),
            "address for registrar connections")
        ("registrar_port", po::value<uint16_t>(&config.registrarPort)->default_value(5000),
            "port for registrar connections")
        ("bind_to_ip", po::value<bool>(&config.bindToIp)->default_value(false),
            "when set to true, binds to the config address; otherwise, binds on any interface")
        ("database_host", po::value<std::string>(&config.chatDatabaseHost)->default_value("127.0.0.1"),
            "hostname or IP address of the MariaDB server")
        ("database_port", po::value<uint16_t>(&config.chatDatabasePort)->default_value(3306),
            "port for the MariaDB server")
        ("database_user", po::value<std::string>(&config.chatDatabaseUser)->default_value("stationchat"),
            "username used to connect to MariaDB")
        ("database_password", po::value<std::string>(&config.chatDatabasePassword)->default_value(""),
            "password used to connect to MariaDB")
        ("database_schema", po::value<std::string>(&config.chatDatabaseSchema)->default_value("stationchat"),
            "schema (database) name used by stationchat")
        ("database_socket", po::value<std::string>(&config.chatDatabaseSocket)->default_value(""),
            "optional UNIX socket path for local MariaDB connections")
        ("website_integration_enabled", po::value<bool>(&config.websiteIntegration.enabled)->default_value(false),
            "when true, publishes chat status information for consumption by the website")
        ("website_user_link_table", po::value<std::string>(&config.websiteIntegration.userLinkTable)->default_value("web_user_avatar"),
            "table used to associate chat avatars with website accounts")
        ("website_online_status_table", po::value<std::string>(&config.websiteIntegration.onlineStatusTable)->default_value("web_avatar_status"),
            "table used to expose avatar online status to the website")
        ("website_mail_table", po::value<std::string>(&config.websiteIntegration.mailTable)->default_value("web_persistent_message"),
            "table used to expose persistent mail to the website")
        ("website_use_separate_database", po::value<bool>(&config.websiteIntegration.useSeparateDatabase)->default_value(false),
            "when true, uses a dedicated database connection for the website integration")
        ("website_database_host", po::value<std::string>(&config.websiteIntegration.databaseHost)->default_value(""),
            "optional override for the website integration database host")
        ("website_database_port", po::value<uint16_t>(&config.websiteIntegration.databasePort)->default_value(0),
            "optional override for the website integration database port")
        ("website_database_user", po::value<std::string>(&config.websiteIntegration.databaseUser)->default_value(""),
            "optional override for the website integration database user")
        ("website_database_password", po::value<std::string>(&config.websiteIntegration.databasePassword)->default_value(""),
            "optional override for the website integration database password")
        ("website_database_schema", po::value<std::string>(&config.websiteIntegration.databaseSchema)->default_value(""),
            "optional override for the website integration database schema")
        ("website_database_socket", po::value<std::string>(&config.websiteIntegration.databaseSocket)->default_value(""),
            "optional override for the website integration database socket path")
        ;

    po::options_description cmdline_options;
    cmdline_options.add(generic).add(options);

    po::options_description config_file_options;
    config_file_options.add(options);

    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(cmdline_options).allow_unregistered().run(), vm);
    po::notify(vm);

    std::ifstream ifs(configFile.c_str());
    if (!ifs) {
        throw std::runtime_error("Cannot open configuration file: " + configFile);
    }

    po::store(po::parse_config_file(ifs, config_file_options), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << cmdline_options << "\n";
        exit(EXIT_SUCCESS);
    }

    return config;
}

#ifdef __GNUC__
void SignalHandler(int sig) {
    const int BACKTRACE_LIMIT = 10;
    void *arr[BACKTRACE_LIMIT];
    auto size = backtrace(arr, BACKTRACE_LIMIT);

    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(arr, size, STDERR_FILENO);
    exit(1);
}
#endif
