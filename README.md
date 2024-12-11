# pin-alert-BSI

This project is a Raspberry Pi-based system for monitoring voltage drops and sending alerts to a server. It uses WiringPi for GPIO control and libcurl for HTTP requests.

## Prerequisites

Make sure you have the following libraries installed:

- WiringPi
- libcurl

You can install them using the following commands:

```sh
sudo apt-get update
sudo apt-get install -y libcurl4-openssl-dev
```
Wiringpi will be installed from source from github
```ssh
sudo apt-get install git
git clone https://github.com/WiringPi/WiringPi
cd WiringPi
./build
```

## Building and running

To build and run the project, use the following command:  

```sh
gcc Main.c -o pinalertv1 -lwiringPi -lcurl && ./pinalertv1
```
This will compile the Main.c file and create an executable named pinalertv1, which will then be executed.


## Planned rewrite: 

### Pi Side

- Check every 30 minutes and send the system status to the API. The status will have an interrupt to update on time.

#### When entering into critical state:
- Update status API to say "Critical".
- Start data collection. Every 10 minutes, collect data from the sensors and send it to the server.
- While in critical state, send data every 10 minutes rather than 30 and update.
- Make a method that the server can send an API request to instantly retrieve data with the status "force get".

### Server Side

- Add a new endpoint for status and a button to force get data.
- Make another new endpoint just for receiving data when in critical state.
- When in critical state, the server needs to make a report of the data that can be viewed easily, showing the change in voltages with graphs, etc.
- When making a report, include the first 2 "System Ok" variables to see the changes in the system.
- Add a button to view critical reports.
- Add API response time and details about how long it takes for the packet and the ping between the device and the server.
- When writing to the database, include the state it is in. If the previous 5 readings are "System Ok", overwrite them rather than create a new entry. This is to prevent the database from filling up with unnecessary entries. All critical readings should be recorded.