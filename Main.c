/*
 *   Author: Max Day
 *   Description: BSI Pin voltage drop system based on api
 *                System will read in voltages using a single channel Optocoupler voltage detection sensor measuign high or low voltages to test if the system is on or not.
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

Posibility of integrating mongoose to make 2 way api. currently curl is only good if u are a client sending stuff to a server but if we need to send stuff back 
we can either use the response code of the status or we can ue mongoose to make 2 way api


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

    work out why firefox just dies when server side is loaded 

*/

/*
//////////////////////////  UPDATE 20/01/2025   //////////////////////////  


This code will be a proof of concept that will only test of the pins are high or low (on or off). status requests will be send through the 
return json to keep it simple and not require 2 API's or worse a websocket

Server side needs to be rewirtten to graph and show data better for when its dropped. also need to make it so we can export jsons.

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

/*
Program received signal SIGSEGV, Segmentation fault.
__GI___libc_realloc (oldmem=0xfbad2a84, bytes=366504103789) at ./malloc/malloc.c:3422
3422    ./malloc/malloc.c: No such file or directory.
(gdb) backtrace
#0  __GI___libc_realloc (oldmem=0xfbad2a84, bytes=366504103789) at ./malloc/malloc.c:3422
#1  0x0000005555551064 in write_callback ()
#2  0x0000007ff7f054fc in ?? () from /lib/aarch64-linux-gnu/libcurl.so.4
#3  0x0000007ff7f152bc in ?? () from /lib/aarch64-linux-gnu/libcurl.so.4
#4  0x0000007ff7efc1f4 in ?? () from /lib/aarch64-linux-gnu/libcurl.so.4
#5  0x0000007ff7efd6a0 in curl_multi_perform () from /lib/aarch64-linux-gnu/libcurl.so.4
#6  0x0000007ff7ed59e8 in curl_easy_perform () from /lib/aarch64-linux-gnu/libcurl.so.4
#7  0x0000005555551264 in send_data_request ()
#8  0x00000055555516fc in main ()
*/





// Configuration
#define API_ENDPOINT "http://192.168.1.92:4000" // API's allow us to have the client ip be dynamic only the servers ip needs to be defined
int CHECK_INTERVAL = 10;  // Default value
int STATUS_INTERVAL = 2;  // Default value
#define COMMAND_CHECK_INTERVAL 1

const int PIN[] = {0,2,3,7,8};

//const int PIN[] = {0,2,3,7,8,9,12,13,14,21};

//TODO: modify it so packages all follow similar structure 
struct ResponseData {
    char *data;
    size_t size;
};


size_t write_callback(void * contents, size_t size, size_t nmemb, void * userp) {
  return size * nmemb;
}

// timestamp, status, pin0, pin2, pin3, pin7, pin8, pin9, pin12, pin13, pin14, pin21

int send_data_request(float PIN[]) {
    CURL *curl;
    CURLcode res;
    int http_code = 0;

    // get local time rather than time when it is received
    time_t now;
    char timestamp[30];
    time(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S.000Z", localtime(&now));

    // Prepare payload
    char payload[512];
    snprintf(payload, sizeof(payload),
        "{\"timestamp\":\"%s\",\"pin0\":%.2f,\"pin2\":%.2f,\"pin3\":%.2f,\"pin7\":%.2f,\"pin8\":%.2f}",
        timestamp, PIN[0], PIN[1], PIN[2], PIN[3], PIN[4]);

    curl = curl_easy_init();
    if (curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_URL, ({
            char url[256];
            snprintf(url, sizeof(url), "%s/sensors", API_ENDPOINT);
            url;
        }));
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);

        res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return http_code;
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
  CURL * curl = curl_easy_init();
  if (curl) {
    char post_data[50];
    snprintf(post_data, sizeof(post_data), "{\"state\": %d}", state);
    struct curl_slist * headers = curl_slist_append(NULL, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, ({
      char url[256];snprintf(url, sizeof(url), "%s/system/state", API_ENDPOINT);url;
    }));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
  }
  return 0;
}


size_t health_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    struct ResponseData *response = (struct ResponseData *)userp;
    size_t realsize = size * nmemb;
    
    response->data = realloc(response->data, response->size + realsize + 1);
    if(response->data == NULL) {
        return 0;
    }
    
    memcpy(&(response->data[response->size]), contents, realsize);
    response->size += realsize;
    response->data[response->size] = 0;

    // Parse the plain-text response to extract intervals
    char *line = strtok(response->data, "\n");
    while (line != NULL) {
        if (strncmp(line, "checkInterval=", 14) == 0) {
            CHECK_INTERVAL = atoi(line + 14);  // Extract the value after 'checkInterval='
        }
        else if (strncmp(line, "statusInterval=", 15) == 0) {
            STATUS_INTERVAL = atoi(line + 15);  // Extract the value after 'statusInterval='
        }
        line = strtok(NULL, "\n");  // Move to the next line
    }
    
    return realsize;
}

int send_health_ping() {
    CURL *curl;
    CURLcode res;
    struct ResponseData response = {0};
    response.data = malloc(1);  // Initial allocation
    response.size = 0;

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, ({
            char url[256];
            snprintf(url, sizeof(url), "%s/health", API_ENDPOINT);
            url;
        }));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, health_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);

        res = curl_easy_perform(curl);
        if(res == CURLE_OK) {
            printf("\n=== Health Check Response ===\n");
            printf("%s\n", response.data);
            printf("===========================\n\n");
        } else {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }

        curl_easy_cleanup(curl);
        free(response.data);
    }
    return (res == CURLE_OK) ? 0 : 1;
}


int main() {
    time_t last_send_time = 0;
    time_t last_health_ping_time = 0;

    if (wiringPiSetup() == -1) {
        fprintf(stderr, "WiringPi setup failed\n");
        return 1;
    }

    for (int i = 0; i < sizeof(PIN) / sizeof(PIN[0]); i++) 
        pinMode(PIN[i], INPUT);
    
    pullUpDnControl(PIN[3], PUD_DOWN); 
    pullUpDnControl(PIN[4], PUD_DOWN);   
    printf("Voltage monitoring started...\n");

    while (1) {
        time_t now = time(NULL);
        float pin_values[5];

        if (now - last_health_ping_time >= STATUS_INTERVAL) {
            send_health_ping();
            last_health_ping_time = now;
        }

        for (int i = 0; i < sizeof(PIN) / sizeof(PIN[0]); i++) 
            pin_values[i] = digitalRead(PIN[i]);
        
        if (now - last_send_time >= CHECK_INTERVAL) {
            last_send_time = now;
            send_data_request(pin_values);
            printf("Sent sensor readings at %s\n", ctime(&now));
        }

        sleep(1);
    }

    return 0;
}
