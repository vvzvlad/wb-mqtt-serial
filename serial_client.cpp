#include <unistd.h>
#include <iostream>

#include "serial_client.h"

TSerialClient::TSerialClient(PAbstractSerialPort port)
    : Port(port),
      Active(false),
      PollInterval(20),
      Callback([](PRegister){}),
      ErrorCallback([](PRegister, bool){}) {}

TSerialClient::~TSerialClient()
{
    if (Active)
        Disconnect();
}

void TSerialClient::AddDevice(PDeviceConfig device_config)
{
    if (Active)
        throw TSerialDeviceException("can't add registers to the active client");
    if (Debug)
        std::cerr << "AddDevice: " << device_config->Id <<
            (device_config->DeviceType.empty() ? "" : " (" + device_config->DeviceType + ")") <<
            " @ " << device_config->SlaveId << " -- protocol: " << device_config->Protocol << std::endl;
    PSlaveEntry entry = TSlaveEntry::Intern(device_config->Protocol, device_config->SlaveId);
    ConfigMap[entry] = device_config;
}

void TSerialClient::AddRegister(PRegister reg)
{
    if (Active)
        throw TSerialDeviceException("can't add registers to the active client");
    if (Handlers.find(reg) != Handlers.end())
        throw TSerialDeviceException("duplicate register");
    Handlers[reg] = CreateRegisterHandler(reg);
    RegList.push_back(reg);
    if (Debug)
        std::cerr << "AddRegister: " << reg << std::endl;
}

void TSerialClient::Connect()
{
    if (Active)
        return;
    if (!Handlers.size())
        throw TSerialDeviceException("no registers defined");
    if (!Port->IsOpen())
        Port->Open();
    PrepareRegisterRanges();
    Active = true;
}

void TSerialClient::Disconnect()
{
    if (Port->IsOpen())
        Port->Close();
    Active = false;
}

void TSerialClient::PrepareRegisterRanges()
{
    // all of this is seemingly slow but it's actually only done once
    RegRanges.clear();
    PSlaveEntry last_slave(0);
    std::list<PRegister> cur_regs;
    auto it = RegList.begin();
    for (;;) {
        bool at_end = it == RegList.end();
        if ((at_end || (*it)->Slave != last_slave) && !cur_regs.empty()) {
            cur_regs.sort([](const PRegister& a, const PRegister& b) {
                    return a->Type < b->Type || (a->Type == b->Type && a->Address < b->Address);
                });
            PSerialDevice dev = GetDevice(last_slave);
            auto ranges = dev->SplitRegisterList(cur_regs);
            RegRanges.insert(RegRanges.end(), ranges.begin(), ranges.end());
            cur_regs.clear();
        }
        if (at_end)
            break;
        last_slave = (*it)->Slave;
        cur_regs.push_back(*it++);
    }
}

void TSerialClient::MaybeUpdateErrorState(PRegister reg, TRegisterHandler::TErrorState state)
{
    if (state != TRegisterHandler::UnknownErrorState && state != TRegisterHandler::ErrorStateUnchanged)
        ErrorCallback(reg, state);
}

void TSerialClient::Flush()
{
    for (const auto& reg: RegList) {
        auto handler = Handlers[reg];
        if (!handler->NeedToFlush())
            continue;
        PrepareToAccessDevice(handler->Device());
        MaybeUpdateErrorState(reg, handler->Flush());
    }
}

void TSerialClient::Cycle()
{
    Connect();

    // FIXME: that's suboptimal polling implementation.
    // Need to implement bunching of device registers.
    // Note that for multi-register values, all values
    // corresponding to single register should be retrieved
    // by single query.
    for (auto range: RegRanges) {
        {
            // Don't hold the lock while flushing
            std::unique_lock<std::mutex> lock(FlushNeededMutex);
            auto wait_until = std::chrono::steady_clock::now() +
                std::chrono::microseconds(PollInterval * 1000 / RegList.size());
            while (FlushNeededCond.wait_until(lock, wait_until, [this](){ return FlushNeeded; })) {
                lock.unlock();
                Flush();
                lock.lock();
                FlushNeeded = false;
            }
        }

        PSerialDevice dev = GetDevice(range->Slave());
        PrepareToAccessDevice(dev);
        dev->ReadRegisterRange(range);
        range->MapRange([this](PRegister reg, uint64_t new_value) {
                bool changed;
                auto handler = Handlers[reg];
                if (!handler->NeedToPoll())
                    return;
                MaybeUpdateErrorState(reg, handler->AcceptDeviceValue(new_value, true, &changed));
                // Note that handler->CurrentErrorState() is not the
                // same as the value returned by handler->AcceptDeviceValue(...),
                // because the latter may be ErrorStateUnchanged.
                if (changed &&
                    handler->CurrentErrorState() != TRegisterHandler::ReadError &&
                    handler->CurrentErrorState() != TRegisterHandler::ReadWriteError)
                    Callback(reg);
            }, [this](PRegister reg) {
                bool changed;
                auto handler = Handlers[reg];
                if (!handler->NeedToPoll())
                    return;
                // TBD: separate AcceptDeviceReadError method (changed is unused here)
                MaybeUpdateErrorState(reg, handler->AcceptDeviceValue(0, false, &changed));
            });
    }
    for (const auto& p: DeviceMap)
        p.second->EndPollCycle();
}

void TSerialClient::WriteSetupRegister(PRegister reg, uint64_t value)
{
    Connect();
    PSerialDevice dev = GetDevice(reg->Slave);
    PrepareToAccessDevice(dev);
    dev->WriteRegister(reg, value);
}

void TSerialClient::SetTextValue(PRegister reg, const std::string& value)
{
    GetHandler(reg)->SetTextValue(value);
}

std::string TSerialClient::GetTextValue(PRegister reg) const
{
    return GetHandler(reg)->TextValue();
}

bool TSerialClient::DidRead(PRegister reg) const
{
    return GetHandler(reg)->DidRead();
}

void TSerialClient::SetCallback(const TSerialClient::TCallback& callback)
{
    Callback = callback;
}

void TSerialClient::SetErrorCallback(const TSerialClient::TErrorCallback& callback)
{
    ErrorCallback = callback;
}

void TSerialClient::SetPollInterval(int interval)
{
    PollInterval = interval;
}

void TSerialClient::SetDebug(bool debug)
{
    Debug = debug;
    Port->SetDebug(debug);
}

bool TSerialClient::DebugEnabled() const {
    return Debug;
}

void TSerialClient::NotifyFlushNeeded() 
{
    std::unique_lock<std::mutex> lock(FlushNeededMutex);
    FlushNeeded = true;
    FlushNeededCond.notify_all();
}

PRegisterHandler TSerialClient::GetHandler(PRegister reg) const
{
    auto it = Handlers.find(reg);
    if (it == Handlers.end())
        throw TSerialDeviceException("register not found");
    return it->second;
}

PRegisterHandler TSerialClient::CreateRegisterHandler(PRegister reg)
{
    return std::make_shared<TRegisterHandler>(shared_from_this(), GetDevice(reg->Slave), reg);
}

PSerialDevice TSerialClient::GetDevice(PSlaveEntry entry)
{
    auto it = DeviceMap.find(entry);
    if (it != DeviceMap.end())
        return it->second;
    auto configIt = ConfigMap.find(entry);
    if (configIt == ConfigMap.end())
        throw TSerialDeviceException("slave not found");

    try {
        return DeviceMap[entry] = TSerialDeviceFactory::CreateDevice(configIt->second, Port);
    } catch (const TSerialDeviceException& e) {
        Disconnect();
        throw TSerialDeviceException(e.what());
    }
}

void TSerialClient::PrepareToAccessDevice(PSerialDevice dev)
{
    if (dev != LastAccessedDevice) {
        LastAccessedDevice = dev;
        dev->Prepare();
    }
}
