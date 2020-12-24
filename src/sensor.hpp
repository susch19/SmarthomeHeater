#include <OneWire.h>
#include <DS18B20.h>
#define _TASK_STD_FUNCTION
#include <TaskSchedulerDeclarations.h>
#include <queue>

class TempSensor
{
public:
    TempSensor();
    typedef std::function<void(float)> tempMeasuremntCallback_t;
    void setup(Scheduler *scheduler);
    void requestTemperature(tempMeasuremntCallback_t callback);
    static const uint8_t sensorPin = D6;

private:
    static OneWire oneWire;
    static DS18B20 sensor;
    Task task;
    std::queue<TempSensor::tempMeasuremntCallback_t> callbacks;
};