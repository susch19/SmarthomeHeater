#include <sensor.hpp>
#include <functional>
#include <memory>


OneWire TempSensor::oneWire(sensorPin);
DS18B20 TempSensor::sensor(&oneWire);
TempSensor::TempSensor()
    : task(TASK_SECOND, TASK_FOREVER, [this]() {
        
        while (callbacks.size() > 0)
        {
            tempMeasuremntCallback_t call = callbacks.front();
            if (!sensor.isConversionComplete()){
                return;
            }
            callbacks.pop();
            call(sensor.getTempC());
        };
        task.disable();
    })
{}

void updateTemp();


void TempSensor::setup(Scheduler *scheduler)
{
    Serial.println(sensor.begin());
    sensor.setResolution(12);
    scheduler->addTask(task);
}


void TempSensor::requestTemperature(tempMeasuremntCallback_t callback)
{
    callbacks.push(callback);
    if (task.isEnabled())
        return;
    sensor.requestTemperatures();
    task.enableDelayed(TASK_SECOND);
}
