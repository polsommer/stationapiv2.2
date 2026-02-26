
#define ELPP_DEFAULT_LOG_FILE "var/log/swgchat.log"
#include "easylogging++.h"

#include "StationChatApp.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/program_options.hpp>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <sstream>
#include <thread>
#include <vector>

#ifdef __GNUC__
#include <execinfo.h>
#endif

INITIALIZE_EASYLOGGINGPP

StationChatConfig BuildConfiguration(int argc, const char* argv[]);

namespace {

std::string FormatDatabaseTarget(const StationChatConfig& config) {
    std::ostringstream stream;
    stream << "host=" << (config.chatDatabaseHost.empty() ? "<empty>" : config.chatDatabaseHost)
           << ";port=" << config.chatDatabasePort
           << ";socket=" << (config.chatDatabaseSocket.empty() ? "<none>" : config.chatDatabaseSocket);
    return stream.str();
}

void ValidateBindableAddress(const std::string& value, const char* key) {
    auto trimmedValue = boost::algorithm::trim_copy(value);
    if (trimmedValue.empty()) {
        throw std::runtime_error(std::string{"Invalid configuration: "} + key + " cannot be empty when bind_to_ip=true");
    }

    boost::system::error_code error;
    boost::asio::ip::make_address(trimmedValue, error);
    if (error) {
        throw std::runtime_error(std::string{"Invalid configuration: "} + key
            + " must be a valid IP address when bind_to_ip=true (got '" + trimmedValue + "')");
    }
}

void ValidateNonZeroPort(uint16_t value, const char* key) {
    if (value == 0) {
        throw std::runtime_error(std::string{"Invalid configuration: "} + key + " must be greater than zero");
    }
}

void ValidateApiVersionRange(const StationChatConfig& config) {
    if (config.apiMinVersion > config.apiMaxVersion) {
        throw std::runtime_error("Invalid configuration: api_min_version cannot be greater than api_max_version");
    }

    if (!config.SupportsApiVersion(config.apiDefaultVersion)) {
        throw std::runtime_error("Invalid configuration: api_default_version must be within api_min_version and api_max_version");
    }
}

void LogStartupConfigurationSummary(const StationChatConfig& config) {
    const auto bindMode = config.bindToIp ? "configured address" : "any interface (0.0.0.0)";
    const auto gatewayBindAddress = config.bindToIp ? config.gatewayAddress : "0.0.0.0";
    const auto registrarBindAddress = config.bindToIp ? config.registrarAddress : "0.0.0.0";

    LOG(INFO) << "Startup configuration summary:";
    LOG(INFO) << "  Gateway bind=" << gatewayBindAddress << ":" << config.gatewayPort
              << " (mode=" << bindMode << "), advertised=" << config.gatewayAddress << ":" << config.gatewayPort;
    LOG(INFO) << "  Registrar bind=" << registrarBindAddress << ":" << config.registrarPort
              << " (mode=" << bindMode << "), advertised=" << config.registrarAddress << ":" << config.registrarPort;
    LOG(INFO) << "  Database target=" << FormatDatabaseTarget(config);
    LOG(INFO) << "  API versions: min=" << config.apiMinVersion
              << ", max=" << config.apiMaxVersion
              << ", default=" << config.apiDefaultVersion;
    LOG(INFO) << "  Login auth: allow_legacy_login=" << (config.allowLegacyLogin ? "true" : "false");
}

GatewayClusterEndpoint ParseGatewayClusterEndpoint(const std::string& definition) {
    using boost::algorithm::trim_copy;

    auto trimmed = trim_copy(definition);
    if (trimmed.empty()) {
        throw std::runtime_error("gateway_cluster entry cannot be empty");
    }

    std::string address;
    std::string remainder;

    if (trimmed.front() == '[') {
        auto closingBracket = trimmed.find(']');
        if (closingBracket == std::string::npos) {
            throw std::runtime_error("Invalid gateway_cluster entry (missing ']'): " + trimmed);
        }

        address = trimmed.substr(1, closingBracket - 1);
        remainder = trimmed.substr(closingBracket + 1);

        if (remainder.empty() || remainder.front() != ':') {
            throw std::runtime_error("Invalid gateway_cluster entry (missing port separator): " + trimmed);
        }

        remainder.erase(remainder.begin());
    } else {
        auto colonPosition = trimmed.find(':');
        if (colonPosition == std::string::npos) {
            throw std::runtime_error("Invalid gateway_cluster entry (missing port): " + trimmed);
        }

        address = trimmed.substr(0, colonPosition);
        remainder = trimmed.substr(colonPosition + 1);
    }

    if (address.empty()) {
        throw std::runtime_error("Invalid gateway_cluster entry (empty address): " + trimmed);
    }

    auto weightSeparator = remainder.find(':');
    auto portToken = remainder.substr(0, weightSeparator);

    if (portToken.empty()) {
        throw std::runtime_error("Invalid gateway_cluster entry (empty port): " + trimmed);
    }

    uint32_t portValue;
    try {
        portValue = static_cast<uint32_t>(std::stoul(portToken));
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid gateway_cluster entry (port is not a number): " + trimmed);
    }

    if (portValue == 0 || portValue > std::numeric_limits<uint16_t>::max()) {
        throw std::runtime_error("Invalid gateway_cluster entry (port out of range): " + trimmed);
    }

    uint16_t weight = 1;
    if (weightSeparator != std::string::npos) {
        auto weightToken = remainder.substr(weightSeparator + 1);
        if (weightToken.empty()) {
            throw std::runtime_error("Invalid gateway_cluster entry (empty weight): " + trimmed);
        }

        uint32_t weightValue;
        try {
            weightValue = static_cast<uint32_t>(std::stoul(weightToken));
        } catch (const std::exception&) {
            throw std::runtime_error("Invalid gateway_cluster entry (weight is not a number): " + trimmed);
        }

        if (weightValue == 0) {
            throw std::runtime_error("Invalid gateway_cluster entry (weight must be greater than zero): " + trimmed);
        }

        if (weightValue > std::numeric_limits<uint16_t>::max()) {
            weightValue = std::numeric_limits<uint16_t>::max();
        }

        weight = static_cast<uint16_t>(weightValue);
    }

    return GatewayClusterEndpoint{address, static_cast<uint16_t>(portValue), weight};
}

} // namespace

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

    LogStartupConfigurationSummary(config);

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
    std::vector<std::string> clusterGateways;

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
        ("gateway_address", po::value<std::string>(&config.gatewayAddress)->default_value("0.0.0.0"),
            "address for gateway connections")
        ("gateway_port", po::value<uint16_t>(&config.gatewayPort)->default_value(5001),
            "port for gateway connections")
        ("registrar_address", po::value<std::string>(&config.registrarAddress)->default_value("0.0.0.0"),
            "address for registrar connections")
        ("registrar_port", po::value<uint16_t>(&config.registrarPort)->default_value(5000),
            "port for registrar connections")
        ("bind_to_ip", po::value<bool>(&config.bindToIp)->default_value(false),
            "when set to true, binds to the config address; otherwise, binds on any interface")
        ("database_host", po::value<std::string>(&config.chatDatabaseHost)->default_value("127.0.0.1"),
            "hostname or IP address of the MariaDB server")
        ("database_port", po::value<uint16_t>(&config.chatDatabasePort)->default_value(3306),
            "port for the MariaDB server")
        ("database_user", po::value<std::string>(&config.chatDatabaseUser)->default_value("swgplus_com"),
            "username used to connect to MariaDB")
        ("database_password", po::value<std::string>(&config.chatDatabasePassword)->default_value(""),
            "password used to connect to MariaDB")
        ("database_schema", po::value<std::string>(&config.chatDatabaseSchema)->default_value("swgplus_com_db"),
            "schema (database) name used by stationchat")
        ("database_socket", po::value<std::string>(&config.chatDatabaseSocket)->default_value(""),
            "optional UNIX socket path for local MariaDB connections")
        ("api_min_version", po::value<uint32_t>(&config.apiMinVersion)->default_value(StationChatConfig::kLegacyApiVersion),
            "minimum supported API version for SETAPIVERSION")
        ("api_max_version", po::value<uint32_t>(&config.apiMaxVersion)->default_value(StationChatConfig::kEnhancedApiVersion),
            "maximum supported API version for SETAPIVERSION")
        ("api_default_version", po::value<uint32_t>(&config.apiDefaultVersion)->default_value(StationChatConfig::kLegacyApiVersion),
            "default API version to advertise when a client does not map to a supported version")
        ("allow_legacy_login", po::value<bool>(&config.allowLegacyLogin)->default_value(true),
            "when false, LOGINAVATAR requires v3 proof-of-authentication fields")
        ("website_integration_enabled", po::value<bool>(&config.websiteIntegration.enabled)->default_value(true),
            "when true, publishes chat status information for consumption by the website")
        ("website_user_link_table", po::value<std::string>(&config.websiteIntegration.userLinkTable)->default_value("web_user_avatar"),
            "table used to associate chat avatars with website accounts")
        ("website_online_status_table", po::value<std::string>(&config.websiteIntegration.onlineStatusTable)->default_value("web_avatar_status"),
            "table used to expose avatar online status to the website")
        ("website_mail_table", po::value<std::string>(&config.websiteIntegration.mailTable)->default_value("web_persistent_message"),
            "table used to expose persistent mail to the website")
        ("website_use_separate_database", po::value<bool>(&config.websiteIntegration.useSeparateDatabase)->default_value(false),
            "when true, uses a dedicated database connection for the website integration")
        ("website_database_host", po::value<std::string>(&config.websiteIntegration.databaseHost)->default_value("127.0.0.1"),
            "optional override for the website integration database host")
        ("website_database_port", po::value<uint16_t>(&config.websiteIntegration.databasePort)->default_value(3306),
            "optional override for the website integration database port")
        ("website_database_user", po::value<std::string>(&config.websiteIntegration.databaseUser)->default_value("swgplus_com"),
            "optional override for the website integration database user")
        ("website_database_password", po::value<std::string>(&config.websiteIntegration.databasePassword)->default_value(""),
            "optional override for the website integration database password")
        ("website_database_schema", po::value<std::string>(&config.websiteIntegration.databaseSchema)->default_value("swgplus_com_db"),
            "optional override for the website integration database schema")
        ("website_database_socket", po::value<std::string>(&config.websiteIntegration.databaseSocket)->default_value(""),
            "optional override for the website integration database socket path")
        ("gateway_cluster", po::value<std::vector<std::string>>(&clusterGateways)->multitoken()->composing(),
            "additional gateway endpoints in host:port[:weight] format for clustering; may be specified multiple times")
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

    config.gatewayCluster.clear();
    for (const auto& entry : clusterGateways) {
        if (entry.empty()) {
            continue;
        }

        auto trimmed = boost::algorithm::trim_copy(entry);
        if (trimmed.empty()) {
            continue;
        }

        config.gatewayCluster.push_back(ParseGatewayClusterEndpoint(trimmed));
    }

    config.NormalizeClusterGateways();

    ValidateNonZeroPort(config.gatewayPort, "gateway_port");
    ValidateNonZeroPort(config.registrarPort, "registrar_port");
    ValidateNonZeroPort(config.chatDatabasePort, "database_port");
    ValidateApiVersionRange(config);

    if (config.websiteIntegration.useSeparateDatabase) {
        ValidateNonZeroPort(config.websiteIntegration.databasePort, "website_database_port");
    }

    if (config.bindToIp) {
        ValidateBindableAddress(config.gatewayAddress, "gateway_address");
        ValidateBindableAddress(config.registrarAddress, "registrar_address");
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
