#pragma once

#include <string>
#include <sstream>
#include <memory>
#include <vector>
#include <exception>
#include <map>

#include "register.h"
#include "portsettings.h"

#if defined(__APPLE__) || defined(__APPLE_CC__)
#   include <json/json.h>
#else
#   include <jsoncpp/json/json.h>
#endif

struct TTemplate {
    TTemplate(const Json::Value& device_data);
    Json::Value DeviceData;
};

typedef std::shared_ptr<TTemplate> PTemplate;

typedef std::map<std::string, PTemplate> TTemplateMap;
typedef std::shared_ptr<TTemplateMap> PTemplateMap;

struct TDeviceChannelConfig {
    TDeviceChannelConfig(std::string name = "", std::string type = "text",
                   std::string device_id = "", int order = 0,
                   std::string on_value = "", int max = - 1, bool read_only = false,
                   const std::vector<PRegisterConfig> regs =
                       std::vector<PRegisterConfig>())
        : Name(name), Type(type), DeviceId(device_id),
          Order(order), OnValue(on_value), Max(max),
          ReadOnly(read_only), RegisterConfigs(regs) {}
    std::string Name;
    std::string Type;
    std::string DeviceId; // FIXME
    int Order;
    std::string OnValue;
    int Max;
    bool ReadOnly;
    std::vector<PRegisterConfig> RegisterConfigs;
};

typedef std::shared_ptr<TDeviceChannelConfig> PDeviceChannelConfig;

struct TDeviceSetupItemConfig {
    TDeviceSetupItemConfig(std::string name, PRegisterConfig reg, int value)
        : Name(name), RegisterConfig(reg), Value(value) {}
    std::string Name;
    PRegisterConfig RegisterConfig;
    int Value;
};

typedef std::shared_ptr<TDeviceSetupItemConfig> PDeviceSetupItemConfig;

static const int DEFAULT_INTER_DEVICE_DELAY_MS = 100;
static const int DEFAULT_ACCESS_LEVEL = 1;
static const int DEFAULT_DEVICE_TIMEOUT_MS = 1000;

struct TDeviceConfig {
    TDeviceConfig(std::string name = "", std::string slave_id = "", std::string protocol = "")
        : Name(name), SlaveId(slave_id), Protocol(protocol) {}

    int NextOrderValue() const { return DeviceChannelConfigs.size() + 1; }
    void AddChannel(PDeviceChannelConfig channel) { DeviceChannelConfigs.push_back(channel); };
    void AddSetupItem(PDeviceSetupItemConfig item) { SetupItemConfigs.push_back(item); }
    std::string Id;
    std::string Name;
    std::string SlaveId;
    std::string DeviceType;
    std::string Protocol;
    std::vector<PDeviceChannelConfig> DeviceChannelConfigs;
    std::vector<PDeviceSetupItemConfig> SetupItemConfigs;
    std::vector<uint8_t> Password;
    std::chrono::milliseconds Delay = std::chrono::milliseconds(DEFAULT_INTER_DEVICE_DELAY_MS);
    int AccessLevel = DEFAULT_ACCESS_LEVEL;
    std::chrono::milliseconds FrameTimeout = std::chrono::milliseconds(-1);
    int MaxRegHole = 0, MaxBitHole = 0;
    int MaxReadRegisters = 1;
    int Stride = 0, Shift = 0;
    PRegisterTypeMap TypeMap = 0;
    std::chrono::microseconds GuardInterval = std::chrono::microseconds(0);
    std::chrono::milliseconds DeviceTimeout = std::chrono::milliseconds(DEFAULT_DEVICE_TIMEOUT_MS);
};

typedef std::shared_ptr<TDeviceConfig> PDeviceConfig;

struct TPortConfig {
    void AddDeviceConfig(PDeviceConfig device_config);
    TSerialPortSettings ConnSettings;
    std::chrono::milliseconds PollInterval = std::chrono::milliseconds(20);
    bool Debug = false;
    int MaxUnchangedInterval;
    std::vector<PDeviceConfig> DeviceConfigs;
};

typedef std::shared_ptr<TPortConfig> PPortConfig;

struct THandlerConfig {
    void AddPortConfig(PPortConfig port_config) {
        port_config->Debug = Debug;
        port_config->MaxUnchangedInterval = MaxUnchangedInterval;
        PortConfigs.push_back(port_config);
    }
    bool Debug = false;
    int MaxUnchangedInterval = -1;
    std::vector<PPortConfig> PortConfigs;
};

typedef std::shared_ptr<THandlerConfig> PHandlerConfig;

class TConfigParserException: public std::exception {
public:
    TConfigParserException(std::string _message): Message("Error parsing config file: " + _message) {}
    const char* what () const throw () {
        return Message.c_str();
    }
private:
    std::string Message;
};

PTemplateMap LoadConfigTemplates(const std::string& templatesDir, const Json::Value& templateSchema);

Json::Value LoadConfigTemplatesSchema(const std::string& templateSchemaFileName, const Json::Value& configSchema);
void AddProtocolType(Json::Value& configSchema, const std::string& protocolType);
void AddRegisterType(Json::Value& configSchema, const std::string& registerType);

typedef std::function<PRegisterTypeMap(PDeviceConfig device_config)> TGetRegisterTypeMapFn;

Json::Value LoadConfigSchema(const std::string& schemaFileName);

PHandlerConfig LoadConfig(const std::string& configFileName, 
                          bool forceDebug,
                          TGetRegisterTypeMapFn getRegisterTypeMapFn,
                          const Json::Value& configSchema,
                          PTemplateMap templates = PTemplateMap());
