# C Reader For DHT11

We encountered some issues with sample code provided with vendor tutorials 
for the DHT11 sensor.  In particular, 
the tutorials we reviewed relied on libraries specific to the Raspberry Pi, and 
more problematically used polling to read pin values for the sensor 
which were in turn used to determine the temperature and relative humidity.  

As we are using a Raspberry Pi Zero 2 W, but not Raspberry Pi OS, we needed to identify a 
more general Linux library for working with GPIO, and decided to use libgpiod.  

Also, as we researched how to read the sensor, which is documented in [1], 
we concluded that a polling method 
(by which we mean here the repeated querying of the pin value) would be unreliable at best, 
and at worst would rarely result in a successful reading of the sensor.  
We instead sought an event-driven approach, in which the code listened for changes 
in the pin values.  A review of libgpiod suggested there were methods to support this.  
Version 2.0 provides examples and documentation indicating how to do this, 
but as our package repo still uses 1.6, for which we did not find doxygen-style API documentation 
or examples, we reviewed the code directly to identify the necessary methods to use.  

References: 
1. [Arduino compatible coding 15: Reading sensor data from DHT-11 without using a library](https://www.engineersgarage.com/articles-arduino-dht11-humidity-temperature-sensor-interfacing/)

