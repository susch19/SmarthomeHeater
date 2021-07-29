#include <painlessMeshPlugins/EspMeshDevice.hpp>
#include <sensor.hpp>
#include <EEPROM_Rotate.h>
#include <LittleFSWrapper.hpp>

class Heater : public EspMeshDevice
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

    struct TimeTempMessageConfig
    {
        uint8_t length;
        TimeTempMessage configs[255];
    };

public:
    Heater() : calibration{DayOfWeekW::Mon, 0, 512} {}
    static const uint8_t HEATERPIN = D3;
    static const uint8_t HEATERPING = D5;
    static const uint8_t SENSORPING = D0;

protected:
    virtual void OnMeshMsgReceived(uint32_t from, const std::string &messageType, const std::string &command, const std::vector<MessageParameter> &parameters) override;
    virtual std::vector<MessageParameter> AdditionalWhoAmIResponseParams() override;
    virtual void preMeshSetup() override;
    virtual void preReboot() ;
    virtual void serverTimeRecieved(timeval tv) override;
    TempSensor tempSensor;

private:
    void decodeSmartHomeTimeTempMessage(const std::string &msg);
    std::vector<TimeTempMessage> stringToTTM(const std::string  &str);
    void tempMeasureCallback(float temp);
    void saveCurrentTime();
    void insert(std::vector<Heater::TimeTempMessage> &cont, Heater::TimeTempMessage value);
    void saveCalibration(TimeTempMessage calibration);
    TimeTempMessage lastSendTemp;
    std::unique_ptr<Task> getTemperaturTask;
    std::unique_ptr<Task> ledWhoIAmTask;
    
    std::vector<TimeTempMessage> config;
    Task checkTemperature;
    TimeTempMessage calibration;
    TimeTempMessage userConf;
    TimeTempMessage lastSendTTM;
    TimeTempMessage lastReceivedTemp;
    bool lastReceivedValid;
    bool debug = false;
    bool disableLED = true;
    bool disableHeating = false;
    bool shouldCalibrate = false;
};