/*
*   Author: Max Day 
*   Description: c test pin thing ill work it out later
*
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <curl/curl.h>
#include <time.h>
#include <wiringPi.h>
#include <signal.h>

/*
* This can be rewritten into a better flow loop taking out the interupt because its basically useless 
* eg: if every 30 minutes u check and 2 minutes after u have checked the power drops but then turns back on just before 
* the next check you will have a false positive. without knowing 

* solution run more checks and store it as a packaged json file in the cache or ram then send it every 30 minutes but record 3x 10minute intervals
* of data


* https://github.com/WiringPi/WiringPi
* libcurl4-openssl-dev


to run : gcc Main.c -o pinalertv1 -lwiringPi -lcurl && ./pinalertv1

*/

/*
Planned rewrite:

    raspi Side:

    - check with check every 30 minutes and send to api the system status. the status will have an interupt. so it updated on time 

when entering into critical state 
    - update status api to say critical 
    - start data collection. every 10 minutes collect data from the sensors and send it to the server
    - while in critical state send data even 10 minutes rather than 30 and update.
    - make a method that the server can send a api request to instantly retrieve data with the status "force get"

    
    Server side:
    - add new endpoint for for status and a button to force get data 
    - make another new endpoint just for recieveing data when in critical state
    - when in critical state the server needs to make a report of the data that can be viewed easily showing the change in voltages 
    with graphs ect...
    - when making a report inclue the first 2 system ok variables to see the changes in the system.
    - add button to view critical reports. 
    - add a api response time and detaisl about how long its taking the packet and the ping between the device and the server
    - when writing to the database include the state it is in. if the previous 5 readings are system ok then overite them rather than create a new entry. 
    this is to prevent th database from filling up with entries that are not needed. all critical readings should be recorded.

*/


// config
#define DIGITAL_PIN1     0   // WiringPi pin number (GPIO 17)
#define API_ENDPOINT    "http://192.168.1.94:4000/sensors" // need to swap out endpoint adresses at later stage.


void voltage_drop_handler(void);
volatile sig_atomic_t voltage_drop_detected = 0;
volatile sig_atomic_t critical_state = 0;

size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    return size * nmemb;
}

int send_api_request(const char* status, int volt, int temp, int temp2) {
    CURL *curl;
    CURLcode res;
    int http_code = 0;

    // log timestamp at device time rather than endpoint time 
    time_t now;
    char timestamp[30];
    time(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S.000Z", localtime(&now));


    char payload[256];
    snprintf(payload, sizeof(payload), 
        "{\"timestamp\":\"%s\",\"status\":\"%s\",\"volt\":%d,\"temp\":%d,\"temp2\":%d}", 
        timestamp, status, volt, temp, temp2);


    curl = curl_easy_init();
    if(curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, API_ENDPOINT);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);

        res = curl_easy_perform(curl);
        
        // response shouldnt be needed
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else {
            char *response = NULL;
            curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &response);
            fprintf(stderr, "Sent to server.\t response : %s\n", response ? response : "No response");
        }
      
        //clean
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    return http_code;
}

//interrupt 
void voltage_drop_handler() {
    voltage_drop_detected = 1;
    critical_state = 1;
}

int main() {
    if (wiringPiSetup() == -1) {
        fprintf(stderr, "WiringPi setup failed\n");
        return 1;
    }

    // Add pins and sensors here. pwr pin 0 will be our default power check
    //posibility of adding battery backup to raspi incase of full power drop
    pinMode(DIGITAL_PIN1, INPUT);
    
    // flop falling edge 
  if (wiringPiISR(DIGITAL_PIN1, INT_EDGE_FALLING, &voltage_drop_handler) < 0) {
    fprintf(stderr, "Unable to setup ISR\n");
    return 1;
}
while(1) {
    if (voltage_drop_detected) { // this is instant read which is not great and causes double critical messages 
        int volt_reading = digitalRead(DIGITAL_PIN1);
        if (volt_reading == LOW) { // double check here ik its bad to but eh
           // send_api_request("Critical", volt_reading, 0, 0);
            voltage_drop_detected = 0; // flag here TODO change later 
            critical_state = 1; 
        }
    }

    // check state 
    if (!critical_state) {
        int current_state = digitalRead(DIGITAL_PIN1);
        send_api_request("System Ok", current_state, 0, 0);
        sleep(10); // Sleep only when not in critical state
    } else {
        int volt_reading = digitalRead(DIGITAL_PIN1);
        if (volt_reading == LOW) { // double check here 
            send_api_request("Critical", volt_reading, 0, 0);
        } else {
            critical_state = 0; // power restored
            send_api_request("System Ok", volt_reading, 0, 0);
        }
        sleep(10); // delay bewteen checks 
    }
}
return 0;
}