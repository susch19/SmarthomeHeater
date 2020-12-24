#include <Device.h>
#include <sensor.hpp>
#include <EEPROM_Rotate.h>

class Heater : public Device
{

private:

    enum class DayOfWeekW : uint8_t
    {
        Mon,
        Tue,
        Wed,
        Thu,
        Fri,
        Sat,
        Sun
    };
    typedef struct TimeTempMessage
    {
        DayOfWeekW DayOfWeek : 3;
        uint16_t Time : 11;
        uint16_t Temp : 10;
        // TimeTempMessage(DayOfWeekW w, uint16_t time, uint16_t temp): DayOfWeek(w),Time(time), Temp(temp){}

        inline bool operator==(TimeTempMessage ttm)
        {
            return ttm.DayOfWeek == DayOfWeek && ttm.Time == Time && ttm.Temp == Temp;
        }

        bool operator==(TimeTempMessage ttm) const
        {
            return ttm.DayOfWeek == DayOfWeek && ttm.Time == Time && ttm.Temp == Temp;
        }

        inline bool operator!=(TimeTempMessage ttm)
        {
            return ttm.DayOfWeek != DayOfWeek || ttm.Time != Time || ttm.Temp != Temp;
        }

        bool operator!=(TimeTempMessage ttm) const
        {
            return ttm.DayOfWeek != DayOfWeek || ttm.Time != Time || ttm.Temp != Temp;
        }
    } __attribute__((packed)) TimeTempMessage;

public:
    Heater(String version) : Device("heater", version), calibration{DayOfWeekW::Mon,0,512} {}
    static const uint8_t HEATERPIN = D3;
    static const uint8_t HEATERPING = D5;
    static const uint8_t SENSORPING = D0;
    virtual void init() override;

protected:
    virtual void OnMsgReceived(uint32_t from, const String &messageType, const String &command, const String &parameter) override;
    virtual std::vector<MessageParameter> AdditionalWhoAmIResponseParams() override;

    TempSensor tempSensor;

private:
    void decodeSmartHomeTimeTempMessage(const String &msg);
    std::vector<TimeTempMessage> stringToTTM(const String &str);
    void tempMeasureCallback(float temp);
    void insert(std::vector<Heater::TimeTempMessage> &cont, Heater::TimeTempMessage value);
    TimeTempMessage lastSendTemp;
    std::unique_ptr<Task> getTemperaturTask;
    std::unique_ptr<Task> ledWhoIAmTask;
    EEPROM_Rotate EEPROMr;
    std::vector<TimeTempMessage> config;
    Task checkTemperature;
    TimeTempMessage calibration;
    TimeTempMessage userConf;
    TimeTempMessage lastSendTTM;
    TimeTempMessage lastReceivedTemp;
    bool lastReceivedValid;
    bool debug = false;
    bool disableHeating = false;
    bool shouldCalibrate = false;
};