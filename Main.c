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

Futher notes:

JSON format for export 
posible sending critical failure package to client through whatsapp or email.
needs to use all GPIO pins. - TODO: make diagram to outline the pinouts and how one pin has to be reserved
voltage of triger pin needs to check tolerance 


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
    - when writing to the database include the state    it is in. if the previous 5 readings are system ok then overite them rather than create a new entry. 
    this is to prevent th database from filling up with entries that are not needed. all critical readings should be recorded.

*/

/*
+--------------+----------------+----------------------+
| Physical Pin | WiringPi Pin   | Purpose              |
+--------------+----------------+----------------------+
|      3       |       8        |                      |
|      5       |       9        |                      |
|      7       |       7        |                      |
|     11       |       0        | voltage drop         |
|     13       |       2        |                      |
|     15       |       3        |                      |
|     19       |      12        |                      |
|     21       |      13        |                      |
|     23       |      14        |                      |
|     29       |      21        |                      |
+--------------+----------------+----------------------+ */


// Configuration
#define API_ENDPOINT    "http://192.168.1.92:4000"
#define DIGITAL_PIN1     0   // WiringPi pin number (GPIO 17)
#define CHECK_INTERVAL  10   // Seconds between checks
#define STATUS_INTERVAL 2    // Seconds between status updates

size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    return size * nmemb;
}


// timestamp, status, pin0, pin2, pin3, pin7, pin8, pin9, pin12, pin13, pin14, pin21
int send_data_request(const int status, int volt, int temp, int temp2) {
    CURL *curl;
    CURLcode res;
    int http_code = 0;

    // get local time rather than time when it is recieved 
    time_t now;
    char timestamp[30];
    time(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S.000Z", localtime(&now));

    // Prepare payload
    char payload[256];
    snprintf(payload, sizeof(payload), 
        "{\"timestamp\":\"%s\",\"status\":\"%d\",\"volt\":%d,\"temp\":%d,\"temp2\":%d}", 
        timestamp, status, volt, temp, temp2);

    curl = curl_easy_init();
    if(curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_URL, ({ char url[256]; snprintf(url, sizeof(url), "%s/sensors", API_ENDPOINT); url; }));
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    return 0;
}

/**
 * Sends a status update to the server.
 *
 * This function sends a POST request to the server with the current system state.
 * - 0: System offline
 * - 1: Critical 
 * - 2: System OK 
 */
int send_status_update(int state) {
    CURL *curl = curl_easy_init();
    if(curl) {
        char post_data[50];
        snprintf(post_data, sizeof(post_data), "{\"state\": %d}", state);
        struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_URL, ({ char url[256]; snprintf(url, sizeof(url), "%s/system/state", API_ENDPOINT); url; }));
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if(headers){
            printf("headers have values\n");
        }

    } 
    return 0;
}


int main() {
    time_t last_check = 0;
    time_t last_status_update = 0;

    // WiringPi setup
    if (wiringPiSetup() == -1) {
        fprintf(stderr, "WiringPi setup failed\n");
        return 1;
    }

    // Set pin mode
    pinMode(DIGITAL_PIN1, INPUT);
    printf("Voltage monitoring started...\n");

    while(1) {
        time_t now = time(NULL);

        if(now - last_check >= CHECK_INTERVAL) {
            last_check = now;
            int current_state = digitalRead(DIGITAL_PIN1);
            const int status = (current_state == LOW) ? 1 : 2;
            send_data_request(status, current_state, 0, 0);
        }

        if(now - last_status_update >= STATUS_INTERVAL) {
            last_status_update = now;
            int state = (digitalRead(DIGITAL_PIN1) == LOW) ? 1 : 2;
            send_status_update(state);
        }

        sleep(1);
    }

    return 0;
}