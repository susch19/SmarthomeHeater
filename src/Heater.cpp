#include <Heater.hpp>
// #include <EEPROM.h>
#include <painlessmesh/base64.hpp>
#include <painlessmesh/protocol.hpp>

// TempSensor tempSensor();
TempSensor::tempMeasuremntCallback_t call;

std::vector<Heater::TimeTempMessage> Heater::stringToTTM(const std::string &s)
{
    size_t size;
    char *buf = const_cast<char *>(s.data());

    Serial.print("got ttm: ");
    for (auto c : s)
    {
        Serial.printf("%X", c);
    }
    Serial.println();

    TimeTempMessage *ttm = reinterpret_cast<TimeTempMessage *>(buf);
    std::vector<TimeTempMessage> ttms;
    for (size_t i = 0; i < s.size() / sizeof(TimeTempMessage); i++)
    {
        auto mtt = ttm[i];
        Serial.printf("Time:%d, Temp:%d, Dow:%d\n", mtt.Time, mtt.Temp, mtt.DayOfWeek);
        ttms.emplace_back(ttm[i]);
    }

    return ttms;
}

void Heater::decodeSmartHomeTimeTempMessage(const std::string &msg)
{
    auto ttms = stringToTTM(msg);
    config.clear();

    for (auto ttm : ttms)
        config.emplace_back(ttm);

    uint8_t count = ttms.size();
    if (count > 0)
    {
        TimeTempMessageConfig ttmConfig;
        ttmConfig.length = count;
        uint8_t count = ttms.size();

        for (size_t i = 0; i < count; i++)
            ttmConfig.configs[i] = config[i];

        fileSystem.writeStruct("/config", ttmConfig);
    }
}

void Heater::OnMeshMsgReceived(uint32_t from, const std::string &messageType, const std::string &command, const std::vector<MessageParameter> &parameters)
{
    if (messageType == "Get")
    {
        if (command == "Temp")
        {
            tempSensor.requestTemperature([from, this](float temp)
                                          { this->sendSingle(from, "Update", "Temp", {temp}); });
        }
    }
    else if (messageType == "Update")
    {
        if (command == "Temp")
        {
            userConf = stringToTTM(parameters.front().get()).back();
            tempSensor.requestTemperature(bind(&Heater::tempMeasureCallback, this, std::placeholders::_1));
        }
        else if (command == "WhoIAm")
        {
            digitalWrite(LED_BUILTIN, LOW);
            ledWhoIAmTask = Device::make_unique<Task>(TASK_SECOND, TASK_ONCE,
                                                      []()
                                                      {
                                                          digitalWrite(LED_BUILTIN, HIGH);
                                                      });
            userScheduler.addTask(*ledWhoIAmTask);
            ledWhoIAmTask->enableDelayed(TASK_MINUTE);
        }
        else if (command == "Mode")
        {
            disableHeating = !disableHeating;
        }
    }
    else if (messageType == "Options")
    {
        if (command == "Temp")
        {
            decodeSmartHomeTimeTempMessage(parameters.front().get());
        }
        else if (command == "Mode")
        {
            debug = !debug;
            disableLED = !disableLED;
            digitalWrite(LED_BUILTIN, disableLED);
            fileSystem.writeStruct("/disableLed", disableLED);
        }
    }
    else if (messageType == "Relay")
    {
        if (command == "Temp")
        {
            auto subStr = parameters.front().get();
            Serial.print("Recieved Temp Callback: ");
            for (auto c : subStr)
            {
                Serial.printf("%X", c);
            }
            Serial.println();
            lastReceivedTemp = stringToTTM(subStr).back();
            if (lastReceivedTemp.Temp > 0)
            {
                shouldCalibrate = true;
                lastReceivedValid = true;
            }
        }
    }
}

std::vector<MessageParameter> Heater::AdditionalWhoAmIResponseParams()
{
    std::vector<MessageParameter> ret;
    if (config.size() > 0)
    {
        auto chars = reinterpret_cast<char *>(&config[0]);
        std::string std;
        std.append(chars, config.size() * sizeof(TimeTempMessage));
        ret = {std};
    }

    return ret;
}

void Heater::insert(std::vector<TimeTempMessage> &cont, TimeTempMessage value)
{
    std::vector<TimeTempMessage>::iterator it = std::lower_bound(cont.begin(), cont.end(), value, [](TimeTempMessage b, TimeTempMessage a)
                                                                 { return (a.DayOfWeek == b.DayOfWeek && a.Time > b.Time) || a.DayOfWeek > b.DayOfWeek; }); // find proper position in descending order
    cont.insert(it, value);                                                                                                                                 // insert before iterator it
}

void Heater::tempMeasureCallback(float temp)
{
    timeval tv;
    gettimeofday(&tv, NULL);
    time_t tmp = tv.tv_sec;
    struct tm tm = *localtime(&tmp);
    tm.tm_wday = (tm.tm_wday + 6) % 7;

    auto tmTime = (uint16_t)(tm.tm_hour * 60 + tm.tm_min);
    if (temp < 0)
        temp = 0.1f;
    TimeTempMessage curTTM = {(DayOfWeekW)tm.tm_wday, tmTime, (uint16_t)(temp * 10)};
    bool sendUpdate = false;

    if ((lastReceivedTemp.Temp > 0) && ((int)lastReceivedTemp.DayOfWeek != tm.tm_wday || tmTime >= lastReceivedTemp.Time + 45))
    {
        if (debug)
            Serial.printf("lastReceivedTemp: dow %d, t: %d | tm: wday %d, t %d\n", lastReceivedTemp.DayOfWeek, lastReceivedTemp.Time, tm.tm_wday, tmTime);
        lastReceivedValid = false;
        lastReceivedTemp.Temp = 0;
    }
    bool newCalibration = false;
    if (shouldCalibrate)
    {

        calibration = TimeTempMessage{curTTM.DayOfWeek, curTTM.Time, (uint16_t)(512 + (curTTM.Temp - lastReceivedTemp.Temp))};
        saveCalibration(calibration); //["{\"Date\":\"08.01.2021 19:08:41\"}"], [{"Date":"08.01.2021 19:09:19"}]
        sendUpdate = true;
        shouldCalibrate = false;
        newCalibration = true;
        if (debug)
            Serial.printf("Calibrating own sensor with offest: %d, mytemp: %d, remoteTemp: %d\n", calibration.Temp, curTTM.Temp, lastReceivedTemp.Temp);
    }

    if ((newCalibration || (curTTM.Temp > lastSendTemp.Temp + 2 || curTTM.Temp < lastSendTemp.Temp - 2 || curTTM.Time > lastSendTemp.Time + 45 || curTTM.Time == 0)) && mesh.isConnected(1))
    {
        lastSendTemp = curTTM;
        sendUpdate = true;
    }

    auto currentTemp = lastReceivedValid ? lastReceivedTemp.Temp : (curTTM.Temp - (calibration.Temp - 512));

    auto configCopy = config;
    if (userConf.Temp > 0.f)
        insert(configCopy, userConf);
    auto res = std::find_if(configCopy.rbegin(), configCopy.rend(), [tm](TimeTempMessage ttm)
                            { return (tm.tm_wday == (int)ttm.DayOfWeek && tm.tm_hour * 60 + tm.tm_min >= ttm.Time) || tm.tm_wday > (int)ttm.DayOfWeek; });
    auto ttm = (res == configCopy.rend()) ? configCopy.back() : *res;

    if (ttm != userConf)
        userConf.Temp = 0.f;

    if (ttm != lastSendTTM && mesh.isConnected(1))
    {
        lastSendTTM = ttm;
        sendUpdate = true;
        // auto chars = reinterpret_cast<uint8_t *>(&(lastSendTTM));
        // std::vector<MessageParameter> ret = {painlessmesh::base64::encode(chars, sizeof(TimeTempMessage)).c_str()};
        // sendSingle(serv, "Options", "Temp", ret);
    }
    else if (debug && !mesh.isConnected(1))
    {
        Serial.println("Not connected to root");
    }
    if (sendUpdate)
    {
        //lastSendTTM, calibration, sendTTM

        TimeTempMessage sendTTM{curTTM.DayOfWeek, curTTM.Time, (uint16_t)(curTTM.Temp - (calibration.Temp - 512))};
        if (disableHeating)
            ttm.Temp = 0.f;

        std::array<TimeTempMessage, 3> ttms = {sendTTM, ttm, calibration};
        auto chars = reinterpret_cast<const char *>(&ttms);
        auto param = std::string(chars, sizeof(TimeTempMessage) * ttms.size());
        std::vector<MessageParameter> ret = {param};
        sendSingle(serv, "Update", "Temp", ret);
    }

    if (debug)
        Serial.printf("Searched through %d records\n", configCopy.size());

    if (currentTemp < ttm.Temp && (!disableHeating || currentTemp < 50))
    {
        digitalWrite(HEATERPIN, HIGH);
        if (debug)
        {
            Serial.println("An");
        }
        if (!disableLED)
            digitalWrite(LED_BUILTIN, LOW);
    }
    else
    {
        digitalWrite(HEATERPIN, LOW);
        if (debug)
        {
            Serial.println("Aus");
        }
        if (!disableLED)
            digitalWrite(LED_BUILTIN, HIGH);
    }
    if (debug)
    {
        Serial.printf("found ttm: te:%d ti:%d d:%d, userttm: ti:%d d:%d, curtmp: %d, currtime:%d\n", ttm.Temp, ttm.Time, ttm.DayOfWeek, userConf.Time, userConf.DayOfWeek, currentTemp, tm.tm_hour * 60 + tm.tm_min);
        // Serial.print(WiFi.localIP)
    }
}

void Heater::preMeshSetup()
{
    pinMode(TempSensor::sensorPin, INPUT);
    pinMode(HEATERPIN, OUTPUT);
    pinMode(HEATERPING, OUTPUT);
    pinMode(SENSORPING, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(HEATERPIN, HIGH);
    digitalWrite(HEATERPING, LOW);
    digitalWrite(SENSORPING, LOW);
    fileSystem.init();

    fileSystem.readStruct("/disableLed", disableLED);
    digitalWrite(LED_BUILTIN, disableLED);

    timeval tv = {.tv_sec = 0, .tv_usec = 0};
    fileSystem.readStruct("/time", tv);
    settimeofday(&tv, NULL);
    Serial.println(tv.tv_sec);
    TimeTempMessageConfig ttmConfig;
    ttmConfig.length = 0;
    fileSystem.readStruct("/config", ttmConfig);

    if (ttmConfig.length > 0)
    {

        for (size_t i = 0; i < ttmConfig.length; i++)
            config.emplace_back(ttmConfig.configs[i]);
    }
    fileSystem.readStruct("/calibration", calibration);

    Serial.println();
    Serial.print(calibration.Temp - 512);
    Serial.println("°C");

    tempSensor.setup(&userScheduler);

    getTemperaturTask = Device::make_unique<Task>(TASK_SECOND * 30, TASK_FOREVER,
                                                  [this]()
                                                  {
                                                      Serial.println("Requesting temp");
                                                      tempSensor.requestTemperature(bind(&Heater::tempMeasureCallback, this, std::placeholders::_1));
                                                  });
    debug = true;
    userScheduler.addTask(*getTemperaturTask);
    getTemperaturTask->enable();
}

void Heater::saveCalibration(TimeTempMessage calibration)
{

    TimeTempMessage val;
    fileSystem.readStruct("/calibration", val);
    if ((calibration.Temp > val.Temp && calibration.Temp - val.Temp > 2) || (calibration.Temp < val.Temp && val.Temp - calibration.Temp > 2))
        fileSystem.writeStruct("calibration", calibration);
}

void Heater::preReboot()
{
    MeshDevice::preReboot();
    saveCurrentTime();
}

void Heater::saveCurrentTime()
{

    timeval tv;
    gettimeofday(&tv, NULL);
    fileSystem.writeStruct("/time", tv);
}

void Heater::serverTimeRecieved(timeval tv)
{
    MeshDevice::serverTimeRecieved(tv);
    saveCurrentTime();
}