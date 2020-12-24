#include <Heater.hpp>
// #include <EEPROM.h>
#include <painlessmesh/base64.hpp>
#include <painlessmesh/protocol.hpp>

// TempSensor tempSensor();
TempSensor::tempMeasuremntCallback_t call;
#define DATA_OFFSET 4

std::vector<Heater::TimeTempMessage> Heater::stringToTTM(const String &str)
{
    size_t size;
    std::string s = painlessmesh::base64::decodeStd(str, size);
    char *buf = const_cast<char *>(s.data());

    TimeTempMessage *ttm = reinterpret_cast<TimeTempMessage *>(buf);
    std::vector<TimeTempMessage> ttms;
    for (size_t i = 0; i < s.size() / sizeof(TimeTempMessage); i++)
        ttms.emplace_back(ttm[i]);

    return ttms;
}

void Heater::decodeSmartHomeTimeTempMessage(const String &msg)
{
    auto ttms = stringToTTM(msg);
    config.clear();

    for (auto ttm : ttms)
        config.emplace_back(ttm);

    uint8_t count = ttms.size();
    if (count > 0)
    {
        EEPROMr.write(DATA_OFFSET, count);
        uint16_t address = DATA_OFFSET;

        for (auto ttm : ttms)
        {
            auto buf = reinterpret_cast<uint8 *>(&ttm);
            EEPROMr.write(++address, buf[0]);
            EEPROMr.write(++address, buf[1]);
            EEPROMr.write(++address, buf[2]);
        }

        EEPROMr.commit();
    }
}

void Heater::OnMsgReceived(uint32_t from, const String &messageType, const String &command, const String &parameter)
{
    if (messageType == "Get")
    {
        if (command == "Temp")
        {
            tempSensor.requestTemperature([from, this](float temp) { this->sendSingle(from, "Update", "Temp", {temp}); });
        }
    }
    else if (messageType == "Update")
    {
        if (command == "Temp")
        {
            userConf = stringToTTM(parameter.substring(2, parameter.length() - 2)).back();
            tempSensor.requestTemperature(bind(&Heater::tempMeasureCallback, this, std::placeholders::_1));
        }
        else if (command == "WhoIAm")
        {
            digitalWrite(LED_BUILTIN, LOW);
            ledWhoIAmTask = Device::make_unique<Task>(TASK_SECOND, TASK_ONCE,
                                                      []() {
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
            decodeSmartHomeTimeTempMessage(parameter.substring(2, parameter.length() - 2));
        }
        else if (command == "Mode")
        {
            debug = !debug;
            digitalWrite(LED_BUILTIN, HIGH);
        }
    }
    else if (messageType == "Relay")
    {
        if (command == "Temp")
        {
            lastReceivedTemp = stringToTTM(parameter.substring(2, parameter.length() - 2)).back();
            shouldCalibrate = true;
            lastReceivedValid = true;
        }
    }
}

std::vector<MessageParameter> Heater::AdditionalWhoAmIResponseParams()
{
    std::vector<MessageParameter> ret;
    if (config.size() > 0)
    {
        auto chars = reinterpret_cast<uint8_t *>(&config[0]);
        ret = {painlessmesh::base64::encode(chars, config.size() * sizeof(TimeTempMessage)).c_str()};
    }

    return ret;
}

void Heater::insert(std::vector<TimeTempMessage> &cont, TimeTempMessage value)
{
    std::vector<TimeTempMessage>::iterator it = std::lower_bound(cont.begin(), cont.end(), value, [](TimeTempMessage b, TimeTempMessage a) {
        return (a.DayOfWeek == b.DayOfWeek && a.Time > b.Time) || a.DayOfWeek > b.DayOfWeek;
    });                     // find proper position in descending order
    cont.insert(it, value); // insert before iterator it
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
    auto res = std::find_if(configCopy.rbegin(), configCopy.rend(), [tm](TimeTempMessage ttm) {
        return (tm.tm_wday == (int)ttm.DayOfWeek && tm.tm_hour * 60 + tm.tm_min >= ttm.Time) || tm.tm_wday > (int)ttm.DayOfWeek;
    });
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
    else if(debug && !mesh.isConnected(1))
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
        auto chars = reinterpret_cast<uint8_t *>(&ttms);
        std::vector<MessageParameter> ret = {painlessmesh::base64::encode(chars, sizeof(sendTTM) * ttms.size()).c_str()};
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
            digitalWrite(LED_BUILTIN, LOW);
        }
    }
    else
    {
        digitalWrite(HEATERPIN, LOW);
        if (debug)
        {
            Serial.println("Aus");
            digitalWrite(LED_BUILTIN, HIGH);
        }
    }
    if (debug)
        Serial.printf("found ttm: te:%d ti:%d d:%d, userttm: ti:%d d:%d, curtmp: %d, currtime:%d\n", ttm.Temp, ttm.Time, ttm.DayOfWeek, userConf.Time, userConf.DayOfWeek, currentTemp, tm.tm_hour * 60 + tm.tm_min);
}

void Heater::init()
{
    pinMode(TempSensor::sensorPin, INPUT);
    pinMode(HEATERPIN, OUTPUT);
    pinMode(HEATERPING, OUTPUT);
    pinMode(SENSORPING, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(HEATERPIN, HIGH);
    digitalWrite(HEATERPING, LOW);
    digitalWrite(SENSORPING, LOW);
    EEPROMr.size(10);
    EEPROMr.begin(4096);
    uint8_t data;

    data = EEPROMr.read(DATA_OFFSET);
    uint16_t address = DATA_OFFSET;
    if (data > 0)
    {
        std::string ttms;
        for (size_t i = 0; i < data * sizeof(TimeTempMessage); i++)
            ttms += EEPROMr.read(++address);

        char *buf = const_cast<char *>(ttms.data());
        TimeTempMessage *ttm = reinterpret_cast<TimeTempMessage *>(buf);

        for (size_t i = 0; i < ttms.size() / sizeof(TimeTempMessage); i++)
            config.emplace_back(ttm[i]);
    }

    tempSensor.setup(&userScheduler);

    getTemperaturTask = Device::make_unique<Task>(TASK_SECOND * 30, TASK_FOREVER,
                                                  [this]() {
                                                      Serial.println("Requesting temp");
                                                      tempSensor.requestTemperature(bind(&Heater::tempMeasureCallback, this, std::placeholders::_1));
                                                  });
    mesh.onPackage(12, [this](painlessmesh::protocol::Variant variant) {
        EEPROMr.backup();
        return true;
    });
    debug = true;
    userScheduler.addTask(*getTemperaturTask);
    // getTemperaturTask->enableDelayed(TASK_SECOND * 10);
    getTemperaturTask->enable();
}

//     EEPROM.begin(4095);
//   for (int i = cfgStart ; i < sizeof(cfg) ; i++) {
//     EEPROM.write(i, 0);
//   }
//   delay(200);
//   EEPROM.commit();
//   EEPROM.end();